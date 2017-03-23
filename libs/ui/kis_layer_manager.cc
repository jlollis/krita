/*
 *  Copyright (C) 2006 Boudewijn Rempt <boud@valdyas.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_layer_manager.h"

#include <QRect>
#include <QApplication>
#include <QCursor>
#include <QString>
#include <QDialog>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QDesktopServices>

#include <kactioncollection.h>
#include <klocalizedstring.h>
#include <QMessageBox>
#include <QUrl>

#include <kis_file_name_requester.h>
#include <kis_icon.h>
#include <KisImportExportManager.h>
#include <KisDocument.h>
#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>
#include <KoPointerEvent.h>
#include <KoColorProfile.h>
#include <KoSelection.h>
#include <KisPart.h>
#include <KisMainWindow.h>

#include <filter/kis_filter_configuration.h>
#include <filter/kis_filter.h>
#include <kis_filter_strategy.h>
#include <generator/kis_generator_layer.h>
#include <kis_file_layer.h>
#include <kis_adjustment_layer.h>
#include <kis_mask.h>
#include <kis_clone_layer.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_paint_device.h>
#include <kis_selection.h>
#include <flake/kis_shape_layer.h>
#include <kis_undo_adapter.h>
#include <kis_painter.h>
#include <metadata/kis_meta_data_store.h>
#include <metadata/kis_meta_data_merge_strategy_registry.h>
#include <kis_psd_layer_style.h>
#include <KisMimeDatabase.h>

#include "KisImportExportManager.h"
#include "kis_config.h"
#include "kis_cursor.h"
#include "dialogs/kis_dlg_adj_layer_props.h"
#include "dialogs/kis_dlg_adjustment_layer.h"
#include "dialogs/kis_dlg_layer_properties.h"
#include "dialogs/kis_dlg_generator_layer.h"
#include "dialogs/kis_dlg_file_layer.h"
#include "dialogs/kis_dlg_layer_style.h"
#include "KisDocument.h"
#include "kis_filter_manager.h"
#include "kis_node_visitor.h"
#include "kis_paint_layer.h"
#include "commands/kis_image_commands.h"
#include "commands/kis_layer_command.h"
#include "commands/kis_node_commands.h"
#include "kis_canvas_resource_provider.h"
#include "kis_selection_manager.h"
#include "kis_statusbar.h"
#include "KisViewManager.h"
#include "kis_zoom_manager.h"
#include "canvas/kis_canvas2.h"
#include "widgets/kis_meta_data_merge_strategy_chooser_widget.h"
#include "widgets/kis_wdg_generator.h"
#include "kis_progress_widget.h"
#include "kis_node_commands_adapter.h"
#include "kis_node_manager.h"
#include "kis_action.h"
#include "kis_action_manager.h"
#include "KisPart.h"
#include "kis_raster_keyframe_channel.h"

#include "kis_signal_compressor_with_param.h"
#include "kis_abstract_projection_plane.h"
#include "commands_new/kis_set_layer_style_command.h"
#include "kis_post_execution_undo_adapter.h"
#include "kis_selection_mask.h"
#include "kis_layer_utils.h"
#include "lazybrush/kis_colorize_mask.h"

#include "KisSaveGroupVisitor.h"

KisLayerManager::KisLayerManager(KisViewManager * view)
    : m_view(view)
    , m_imageView(0)
    , m_imageFlatten(0)
    , m_imageMergeLayer(0)
    , m_groupLayersSave(0)
    , m_imageResizeToLayer(0)
    , m_flattenLayer(0)
    , m_rasterizeLayer(0)
    , m_commandsAdapter(new KisNodeCommandsAdapter(m_view))
    , m_layerStyle(0)
{
}

KisLayerManager::~KisLayerManager()
{
    delete m_commandsAdapter;
}

void KisLayerManager::setView(QPointer<KisView>view)
{
    m_imageView = view;
}

KisLayerSP KisLayerManager::activeLayer()
{
    if (m_imageView) {
        return m_imageView->currentLayer();
    }
    return 0;
}

KisPaintDeviceSP KisLayerManager::activeDevice()
{
    if (activeLayer()) {
        return activeLayer()->paintDevice();
    }
    return 0;
}

void KisLayerManager::activateLayer(KisLayerSP layer)
{
    if (m_imageView) {
        emit sigLayerActivated(layer);
        layersUpdated();
        if (layer) {
            m_view->resourceProvider()->slotNodeActivated(layer.data());
        }
    }
}


void KisLayerManager::setup(KisActionManager* actionManager)
{
    m_imageFlatten = actionManager->createAction("flatten_image");
    connect(m_imageFlatten, SIGNAL(triggered()), this, SLOT(flattenImage()));

    m_imageMergeLayer = actionManager->createAction("merge_layer");
    connect(m_imageMergeLayer, SIGNAL(triggered()), this, SLOT(mergeLayer()));

    m_flattenLayer = actionManager->createAction("flatten_layer");
    connect(m_flattenLayer, SIGNAL(triggered()), this, SLOT(flattenLayer()));

    m_rasterizeLayer = actionManager->createAction("rasterize_layer");
    connect(m_rasterizeLayer, SIGNAL(triggered()), this, SLOT(rasterizeLayer()));

    m_groupLayersSave = actionManager->createAction("save_groups_as_images");
    connect(m_groupLayersSave, SIGNAL(triggered()), this, SLOT(saveGroupLayers()));

    m_convertGroupAnimated = actionManager->createAction("convert_group_to_animated");
    connect(m_convertGroupAnimated, SIGNAL(triggered()), this, SLOT(convertGroupToAnimated()));

    m_imageResizeToLayer = actionManager->createAction("resizeimagetolayer");
    connect(m_imageResizeToLayer, SIGNAL(triggered()), this, SLOT(imageResizeToActiveLayer()));

    KisAction *action = actionManager->createAction("trim_to_image");
    connect(action, SIGNAL(triggered()), this, SLOT(trimToImage()));

    m_layerStyle  = actionManager->createAction("layer_style");
    connect(m_layerStyle, SIGNAL(triggered()), this, SLOT(layerStyle()));

}

void KisLayerManager::updateGUI()
{
    KisImageWSP image = m_view->image();

    KisLayerSP layer;
    qint32 nlayers = 0;

    if (image) {
        layer = activeLayer();
        nlayers = image->nlayers();
    }

    // XXX these should be named layer instead of image
    m_imageFlatten->setEnabled(nlayers > 1);
    m_imageMergeLayer->setEnabled(nlayers > 1 && layer && layer->prevSibling());
    m_flattenLayer->setEnabled(nlayers > 1 && layer && layer->firstChild());

    if (m_view->statusBar())
        m_view->statusBar()->setProfile(image);
}

void KisLayerManager::imageResizeToActiveLayer()
{
    KisLayerSP layer;
    KisImageWSP image = m_view->image();

    if (image && (layer = activeLayer())) {
        QRect cropRect = layer->projection()->nonDefaultPixelArea();
        if (!cropRect.isEmpty()) {
            image->cropImage(cropRect);
        } else {
            m_view->showFloatingMessage(
                i18nc("floating message in layer manager",
                      "Layer is empty "),
                QIcon(), 2000, KisFloatingMessage::Low);
        }
    }
}

void KisLayerManager::trimToImage()
{
    KisImageWSP image = m_view->image();
    if (image) {
        image->cropImage(image->bounds());
    }
}

void KisLayerManager::layerProperties()
{
    if (!m_view) return;
    if (!m_view->document()) return;

    KisLayerSP layer = activeLayer();
    QList<KisNodeSP> selectedNodes = m_view->nodeManager()->selectedNodes();
    const bool multipleLayersSelected = selectedNodes.size() > 1;

    if (!layer) return;

    KisAdjustmentLayerSP alayer = KisAdjustmentLayerSP(dynamic_cast<KisAdjustmentLayer*>(layer.data()));
    KisGeneratorLayerSP glayer = KisGeneratorLayerSP(dynamic_cast<KisGeneratorLayer*>(layer.data()));

    if (alayer && !multipleLayersSelected) {

        KisPaintDeviceSP dev = alayer->projection();

        KisDlgAdjLayerProps dlg(alayer, alayer.data(), dev, m_view, alayer->filter().data(), alayer->name(), i18n("Filter Layer Properties"), m_view->mainWindow(), "dlgadjlayerprops");
        dlg.resize(dlg.minimumSizeHint());


        KisFilterConfigurationSP configBefore(alayer->filter());
        KIS_ASSERT_RECOVER_RETURN(configBefore);
        QString xmlBefore = configBefore->toXML();


        if (dlg.exec() == QDialog::Accepted) {

            alayer->setName(dlg.layerName());

            KisFilterConfigurationSP configAfter(dlg.filterConfiguration());
            Q_ASSERT(configAfter);
            QString xmlAfter = configAfter->toXML();

            if(xmlBefore != xmlAfter) {
                KisChangeFilterCmd *cmd
                   = new KisChangeFilterCmd(alayer,
                                             configBefore->name(),
                                             xmlBefore,
                                             configAfter->name(),
                                             xmlAfter,
                                             false);
                // FIXME: check whether is needed
                cmd->redo();
                m_view->undoAdapter()->addCommand(cmd);
                m_view->document()->setModified(true);
            }
        }
        else {
            KisFilterConfigurationSP configAfter(dlg.filterConfiguration());
            Q_ASSERT(configAfter);
            QString xmlAfter = configAfter->toXML();

            if(xmlBefore != xmlAfter) {
                alayer->setFilter(KisFilterRegistry::instance()->cloneConfiguration(configBefore.data()));
                alayer->setDirty();
            }
        }
    }
    else if (glayer && !multipleLayersSelected) {

        KisDlgGeneratorLayer dlg(glayer->name(), m_view, m_view->mainWindow());
        dlg.setCaption(i18n("Fill Layer Properties"));

        KisFilterConfigurationSP configBefore(glayer->filter());
        Q_ASSERT(configBefore);
        QString xmlBefore = configBefore->toXML();

        dlg.setConfiguration(configBefore.data());
        dlg.resize(dlg.minimumSizeHint());

        if (dlg.exec() == QDialog::Accepted) {

            glayer->setName(dlg.layerName());

            KisFilterConfigurationSP configAfter(dlg.configuration());
            Q_ASSERT(configAfter);
            QString xmlAfter = configAfter->toXML();

            if(xmlBefore != xmlAfter) {
                KisChangeFilterCmd *cmd
                        = new KisChangeFilterCmd(glayer,
                                                 configBefore->name(),
                                                 xmlBefore,
                                                 configAfter->name(),
                                                 xmlAfter,
                                                 true);
                // FIXME: check whether is needed
                cmd->redo();
                m_view->undoAdapter()->addCommand(cmd);
                m_view->document()->setModified(true);
            }

        }
    } else { // If layer == normal painting layer, vector layer, or group layer
        QList<KisNodeSP> selectedNodes = m_view->nodeManager()->selectedNodes();

        KisDlgLayerProperties *dialog = new KisDlgLayerProperties(selectedNodes, m_view);
        dialog->resize(dialog->minimumSizeHint());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        Qt::WindowFlags flags = dialog->windowFlags();
        dialog->setWindowFlags(flags | Qt::WindowStaysOnTopHint | Qt::Dialog);
        dialog->show();

    }
}

void KisLayerManager::convertNodeToPaintLayer(KisNodeSP source)
{
    KisImageWSP image = m_view->image();
    if (!image) return;


    KisLayer *srcLayer = qobject_cast<KisLayer*>(source.data());
    if (srcLayer && (srcLayer->inherits("KisGroupLayer") || srcLayer->layerStyle() || srcLayer->childCount() > 0)) {
        image->flattenLayer(srcLayer);
        return;
    }


    KisPaintDeviceSP srcDevice =
        source->paintDevice() ? source->projection() : source->original();

    bool putBehind = false;
    QString newCompositeOp = source->compositeOpId();
    KisColorizeMask *colorizeMask = dynamic_cast<KisColorizeMask*>(source.data());
    if (colorizeMask) {
        srcDevice = colorizeMask->coloringProjection();
        putBehind = colorizeMask->compositeOpId() == COMPOSITE_BEHIND;
        if (putBehind) {
            newCompositeOp = COMPOSITE_OVER;
        }
    }

    if (!srcDevice) return;

    KisPaintDeviceSP clone;

    if (*srcDevice->colorSpace() !=
        *srcDevice->compositionSourceColorSpace()) {

        clone = new KisPaintDevice(srcDevice->compositionSourceColorSpace());

        QRect rc(srcDevice->extent());
        KisPainter::copyAreaOptimized(rc.topLeft(), srcDevice, clone, rc);

    } else {
        clone = new KisPaintDevice(*srcDevice);
    }

    KisLayerSP layer = new KisPaintLayer(image,
                                         source->name(),
                                         source->opacity(),
                                         clone);
    layer->setCompositeOpId(newCompositeOp);

    KisNodeSP parent = source->parent();
    KisNodeSP above = source;

    while (parent && !parent->allowAsChild(layer)) {
        above = above->parent();
        parent = above ? above->parent() : 0;
    }

    if (putBehind && above == source->parent()) {
        above = above->prevSibling();
    }

    m_commandsAdapter->beginMacro(kundo2_i18n("Convert to a Paint Layer"));
    m_commandsAdapter->addNode(layer, parent, above);
    m_commandsAdapter->removeNode(source);
    m_commandsAdapter->endMacro();

}

void KisLayerManager::convertGroupToAnimated()
{
    KisGroupLayerSP group = dynamic_cast<KisGroupLayer*>(activeLayer().data());
    if (group.isNull()) return;

    KisPaintLayerSP animatedLayer = new KisPaintLayer(m_view->image(), group->name(), OPACITY_OPAQUE_U8);
    animatedLayer->enableAnimation();
    KisRasterKeyframeChannel *contentChannel = dynamic_cast<KisRasterKeyframeChannel*>(
            animatedLayer->getKeyframeChannel(KisKeyframeChannel::Content.id(), true));
    KIS_ASSERT_RECOVER_RETURN(contentChannel);

    KisNodeSP child = group->firstChild();
    int time = 0;
    while (child) {
        contentChannel->importFrame(time, child->projection(), NULL);
        time++;

        child = child->nextSibling();
    }

    m_commandsAdapter->beginMacro(kundo2_i18n("Convert to an animated layer"));
    m_commandsAdapter->addNode(animatedLayer, group->parent(), group);
    m_commandsAdapter->removeNode(group);
    m_commandsAdapter->endMacro();
}

void KisLayerManager::adjustLayerPosition(KisNodeSP node, KisNodeSP activeNode, KisNodeSP &parent, KisNodeSP &above)
{
    Q_ASSERT(activeNode);

    parent = activeNode;
    above = parent->lastChild();

    while (parent &&
           (!parent->allowAsChild(node) || parent->userLocked())) {

        above = parent;
        parent = parent->parent();
    }

    if (!parent) {
        warnKrita << "KisLayerManager::adjustLayerPosition:"
                   << "No node accepted newly created node";

        parent = m_view->image()->root();
        above = parent->lastChild();
    }
}

void KisLayerManager::addLayerCommon(KisNodeSP activeNode, KisLayerSP layer, bool updateImage)
{
    KisNodeSP parent;
    KisNodeSP above;
    adjustLayerPosition(layer, activeNode, parent, above);


    KisGroupLayer *group = dynamic_cast<KisGroupLayer*>(parent.data());
    const bool parentForceUpdate = group && !group->projectionIsValid();
    updateImage |= parentForceUpdate;

    m_commandsAdapter->addNode(layer, parent, above, updateImage, updateImage);
}

KisLayerSP KisLayerManager::addLayer(KisNodeSP activeNode)
{
    KisLayerSP layer = KisLayerUtils::constructDefaultLayer(m_view->image());
    addLayerCommon(activeNode, layer, false);

    return layer;
}

void KisLayerManager::addGroupLayer(KisNodeSP activeNode)
{
    KisImageWSP image = m_view->image();
    addLayerCommon(activeNode,
                   new KisGroupLayer(image.data(), image->nextLayerName(), OPACITY_OPAQUE_U8), false);
}

void KisLayerManager::addCloneLayer(KisNodeSP activeNode)
{
    KisImageWSP image = m_view->image();
    addLayerCommon(activeNode,
                   new KisCloneLayer(activeLayer(), image.data(), image->nextLayerName(), OPACITY_OPAQUE_U8));
}

void KisLayerManager::addShapeLayer(KisNodeSP activeNode)
{
    if (!m_view) return;
    if (!m_view->document()) return;

    KisImageWSP image = m_view->image();
    KisShapeLayerSP layer = new KisShapeLayer(m_view->document()->shapeController(), image.data(), image->nextLayerName(), OPACITY_OPAQUE_U8);

    addLayerCommon(activeNode, layer, false);
}

void KisLayerManager::addAdjustmentLayer(KisNodeSP activeNode)
{
    KisImageWSP image = m_view->image();

    KisSelectionSP selection = m_view->selection();
    KisAdjustmentLayerSP adjl = addAdjustmentLayer(activeNode, QString(), 0, selection);
    image->refreshGraph();

    KisPaintDeviceSP previewDevice = new KisPaintDevice(*adjl->original());

    KisDlgAdjustmentLayer dlg(adjl, adjl.data(), previewDevice, image->nextLayerName(), i18n("New Filter Layer"), m_view);
    dlg.resize(dlg.minimumSizeHint());

    // ensure that the device may be free'd by the dialog
    // when it is not needed anymore
    previewDevice = 0;

    if (dlg.exec() != QDialog::Accepted || adjl->filter().isNull()) {
        // XXX: add messagebox warning if there's no filter set!
        m_commandsAdapter->undoLastCommand();
    } else {
        adjl->setName(dlg.layerName());
    }

}

KisAdjustmentLayerSP KisLayerManager::addAdjustmentLayer(KisNodeSP activeNode, const QString & name,
                                                         KisFilterConfigurationSP  filter, KisSelectionSP selection)
{
    KisImageWSP image = m_view->image();
    KisAdjustmentLayerSP layer = new KisAdjustmentLayer(image, name, filter, selection);
    addLayerCommon(activeNode, layer);

    return layer;
}

void KisLayerManager::addGeneratorLayer(KisNodeSP activeNode)
{
    KisImageWSP image = m_view->image();

    KisDlgGeneratorLayer dlg(image->nextLayerName(), m_view, m_view->mainWindow());
    dlg.resize(dlg.minimumSizeHint());

    if (dlg.exec() == QDialog::Accepted) {
        KisSelectionSP selection = m_view->selection();
        KisFilterConfigurationSP  generator = dlg.configuration();
        QString name = dlg.layerName();

        addLayerCommon(activeNode,
            new KisGeneratorLayer(image, name, generator, selection));
    }

}

void KisLayerManager::rotateLayer(double radians)
{
    if (!m_view->image()) return;

    KisLayerSP layer = activeLayer();
    if (!layer) return;

    if (!m_view->blockUntilOperationsFinished(m_view->image())) return;

    m_view->image()->rotateNode(layer, radians);
}

void KisLayerManager::flattenImage()
{
    KisImageSP image = m_view->image();

    if (!m_view->blockUntilOperationsFinished(image)) return;

    if (image) {
        bool doIt = true;

        if (image->nHiddenLayers() > 0) {
            int answer = QMessageBox::warning(m_view->mainWindow(),
                                              i18nc("@title:window", "Flatten Image"),
                                              i18n("The image contains hidden layers that will be lost. Do you want to flatten the image?"),
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);

            if (answer != QMessageBox::Yes) {
                doIt = false;
            }
        }

        if (doIt) {
            image->flatten();
        }
    }
}

inline bool isSelectionMask(KisNodeSP node) {
    return dynamic_cast<KisSelectionMask*>(node.data());
}

bool tryMergeSelectionMasks(KisNodeSP currentNode, KisImageSP image)
{
    bool result = false;

    KisNodeSP prevNode = currentNode->prevSibling();
    if (isSelectionMask(currentNode) &&
        prevNode && isSelectionMask(prevNode)) {

        QList<KisNodeSP> mergedNodes;
        mergedNodes.append(currentNode);
        mergedNodes.append(prevNode);

        image->mergeMultipleLayers(mergedNodes, currentNode);

        result = true;
    }

    return result;
}

void KisLayerManager::mergeLayer()
{
    KisImageSP image = m_view->image();
    if (!image) return;

    KisLayerSP layer = activeLayer();
    if (!layer) return;

    if (!m_view->blockUntilOperationsFinished(image)) return;

    QList<KisNodeSP> selectedNodes = m_view->nodeManager()->selectedNodes();
    if (selectedNodes.size() > 1) {
        image->mergeMultipleLayers(selectedNodes, m_view->activeNode());

    } else if (!tryMergeSelectionMasks(m_view->activeNode(), image)) {

        if (!layer->prevSibling()) return;
        KisLayer *prevLayer = qobject_cast<KisLayer*>(layer->prevSibling().data());
        if (!prevLayer) return;

        if (layer->metaData()->isEmpty() && prevLayer->metaData()->isEmpty()) {
            image->mergeDown(layer, KisMetaData::MergeStrategyRegistry::instance()->get("Drop"));
        }
        else {
            const KisMetaData::MergeStrategy* strategy = KisMetaDataMergeStrategyChooserWidget::showDialog(m_view->mainWindow());
            if (!strategy) return;
            image->mergeDown(layer, strategy);
        }
    }

    m_view->updateGUI();
}

void KisLayerManager::flattenLayer()
{
    KisImageSP image = m_view->image();
    if (!image) return;

    KisLayerSP layer = activeLayer();
    if (!layer) return;

    if (!m_view->blockUntilOperationsFinished(image)) return;

    image->flattenLayer(layer);
    m_view->updateGUI();
}

void KisLayerManager::rasterizeLayer()
{
    KisImageSP image = m_view->image();
    if (!image) return;

    KisLayerSP layer = activeLayer();
    if (!layer) return;

    if (!m_view->blockUntilOperationsFinished(image)) return;

    KisPaintLayerSP paintLayer = new KisPaintLayer(image, layer->name(), layer->opacity());
    KisPainter gc(paintLayer->paintDevice());
    QRect rc = layer->projection()->exactBounds();
    gc.bitBlt(rc.topLeft(), layer->projection(), rc);

    m_commandsAdapter->beginMacro(kundo2_i18n("Rasterize Layer"));
    m_commandsAdapter->addNode(paintLayer.data(), layer->parent().data(), layer.data());

    int childCount = layer->childCount();
    for (int i = 0; i < childCount; i++) {
        m_commandsAdapter->moveNode(layer->firstChild(), paintLayer, paintLayer->lastChild());
    }
    m_commandsAdapter->removeNode(layer);
    m_commandsAdapter->endMacro();
    updateGUI();
}


void KisLayerManager::layersUpdated()
{
    KisLayerSP layer = activeLayer();
    if (!layer) return;

    m_view->updateGUI();
}

void KisLayerManager::saveGroupLayers()
{
    QStringList listMimeFilter = KisImportExportManager::mimeFilter(KisImportExportManager::Export);

    KoDialog dlg;
    QWidget *page = new QWidget(&dlg);
    dlg.setMainWidget(page);
    QBoxLayout *layout = new QVBoxLayout(page);

    KisFileNameRequester *urlRequester = new KisFileNameRequester(page);
    urlRequester->setMode(KoFileDialog::SaveFile);
    if (m_view->document()->url().isLocalFile()) {
        urlRequester->setStartDir(QFileInfo(m_view->document()->url().toLocalFile()).absolutePath());
    }
    urlRequester->setMimeTypeFilters(listMimeFilter);
    urlRequester->setFileName(m_view->document()->url().toLocalFile());
    layout->addWidget(urlRequester);

    QCheckBox *chkInvisible = new QCheckBox(i18n("Convert Invisible Groups"), page);
    chkInvisible->setChecked(false);
    layout->addWidget(chkInvisible);
    QCheckBox *chkDepth = new QCheckBox(i18n("Export Only Toplevel Groups"), page);
    chkDepth->setChecked(true);
    layout->addWidget(chkDepth);

    if (!dlg.exec()) return;

    QString path = urlRequester->fileName();

    if (path.isEmpty()) return;

    QFileInfo f(path);

    QString mimeType= KisMimeDatabase::mimeTypeForFile(f.fileName());
    if (mimeType.isEmpty()) {
        mimeType = "image/png";
    }
    QString extension = KisMimeDatabase::suffixesForMimeType(mimeType).first();
    QString basename = f.baseName();

    KisImageSP image = m_view->image();
    if (!image) return;

    KisSaveGroupVisitor v(image, chkInvisible->isChecked(), chkDepth->isChecked(), f.absolutePath(), basename, extension, mimeType);
    image->rootLayer()->accept(v);

}

bool KisLayerManager::activeLayerHasSelection()
{
    return (activeLayer()->selection() != 0);
}

void KisLayerManager::addFileLayer(KisNodeSP activeNode)
{

    QString basePath;
    QUrl url = m_view->document()->url();
    if (url.isLocalFile()) {
        basePath = QFileInfo(url.toLocalFile()).absolutePath();
    }
    KisImageWSP image = m_view->image();

    KisDlgFileLayer dlg(basePath, image->nextLayerName(), m_view->mainWindow());
    dlg.resize(dlg.minimumSizeHint());

    if (dlg.exec() == QDialog::Accepted) {
        QString name = dlg.layerName();
        QString fileName = dlg.fileName();

        if(fileName.isEmpty()){
            QMessageBox::critical(m_view->mainWindow(), i18nc("@title:window", "Krita"), i18n("No file name specified"));
            return;
        }

        KisFileLayer::ScalingMethod scalingMethod = dlg.scaleToImageResolution();

        addLayerCommon(activeNode,
                       new KisFileLayer(image, basePath, fileName, scalingMethod, name, OPACITY_OPAQUE_U8));
    }

}

void updateLayerStyles(KisLayerSP layer, KisDlgLayerStyle *dlg)
{
    KisSetLayerStyleCommand::updateLayerStyle(layer, dlg->style()->clone());
}

void KisLayerManager::layerStyle()
{
    KisImageWSP image = m_view->image();
    if (!image) return;

    KisLayerSP layer = activeLayer();
    if (!layer) return;

    if (!m_view->blockUntilOperationsFinished(image)) return;

    KisPSDLayerStyleSP oldStyle;
    if (layer->layerStyle()) {
        oldStyle = layer->layerStyle()->clone();
    }
    else {
        oldStyle = toQShared(new KisPSDLayerStyle());
    }

    KisDlgLayerStyle dlg(oldStyle->clone(), m_view->resourceProvider());

    std::function<void ()> updateCall(std::bind(updateLayerStyles, layer, &dlg));
    SignalToFunctionProxy proxy(updateCall);
    connect(&dlg, SIGNAL(configChanged()), &proxy, SLOT(start()));

    if (dlg.exec() == QDialog::Accepted) {
        KisPSDLayerStyleSP newStyle = dlg.style();

        KUndo2CommandSP command = toQShared(
            new KisSetLayerStyleCommand(layer, oldStyle, newStyle));

        image->postExecutionUndoAdapter()->addCommand(command);
    }
}


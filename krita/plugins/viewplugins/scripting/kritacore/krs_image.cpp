/*
 *  Copyright (c) 2005 Cyrille Berger <cberger@cberger.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Library General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "krs_image.h"

#include <kis_colorspace_factory_registry.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_meta_registry.h>

#include "krs_layer.h"

namespace Kross {

namespace KritaCore {

    Image::Image(KisImageSP image, KisDoc* doc)
    : Kross::Api::Class<Image>("KritaImage"), m_image(image), m_doc(doc)
{
    addFunction("getActiveLayer", &Image::getActiveLayer);
    addFunction("getWidth", &Image::getWidth);
    addFunction("getHeight", &Image::getHeight);
    addFunction("convertToColorspace", &Image::convertToColorspace, Kross::Api::ArgumentList() << Kross::Api::Argument("Kross::Api::Variant::String") );
}


Image::~Image()
{
}

const QString Image::getClassName() const {
    return "Kross::KritaCore::Image";
}

Kross::Api::Object::Ptr Image::getActiveLayer(Kross::Api::List::Ptr)
{
    return new Layer(m_image->activeLayer(), m_doc);
}
Kross::Api::Object::Ptr Image::getWidth(Kross::Api::List::Ptr)
{
    return new Kross::Api::Variant(m_image->width());
}
Kross::Api::Object::Ptr Image::getHeight(Kross::Api::List::Ptr)
{
    return new Kross::Api::Variant(m_image->height());
}
Kross::Api::Object::Ptr Image::convertToColorspace(Kross::Api::List::Ptr args)
{
    KisColorSpace * dstCS = KisMetaRegistry::instance()->csRegistry()->getColorSpace(KisID(Kross::Api::Variant::toString(args->item(0)), ""), "");
    if(!dstCS)
    {
        // FIXME: inform user
        kdDebug() << Kross::Api::Variant::toString(args->item(0)) << " colorspace is not available, please check your installation." << endl;
        return 0;
    }
    m_image->convertTo(dstCS);
    return 0;
}


}

}

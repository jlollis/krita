/* This file is part of the KDE project
 * Copyright (C) 2015 Denis Kuplyakov <dener.kup@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SECTIONSSPLITDIALOG_H
#define SECTIONSSPLITDIALOG_H

#include <KoDialog.h>

class KoTextEditor;
class KoSection;
class KoSectionModel;

#include <ui_SectionsSplitDialog.h>
class SectionsSplitDialog : public KoDialog
{
    Q_OBJECT

public:
    explicit SectionsSplitDialog(QWidget *parent, KoTextEditor *editor);

private Q_SLOTS:
    void beforeListSelection();
    void afterListSelection();

    void okClicked();

private:
    Ui::SectionsSplitDialog m_widget;
    KoTextEditor *m_editor;
};

#endif //SECTIONSSPLITDIALOG_H

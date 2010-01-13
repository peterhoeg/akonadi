/*
    Copyright (c) 2006 - 2007 Volker Krause <vkrause@kde.org>
    Copyright (c) 2008 Stephen Kelly <steveire@gmail.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#ifndef AKONADI_ENTITYTREEVIEW_H
#define AKONADI_ENTITYTREEVIEW_H

#include "akonadi_export.h"

#include <QtGui/QTreeView>

class KXMLGUIClient;
class QDragMoveEvent;

namespace Akonadi
{

class Collection;
class Item;

/**
 * @short A view to show an item/collection tree provided by an EntityTreeModel.
 *
 * When a KXmlGuiWindow is passed to the constructor, the XMLGUI
 * defined context menu @c akonadi_collectionview_contextmenu or
 * @c akonadi_itemview_contextmenu is used if available.
 *
 * Example:
 *
 * @code
 *
 * using namespace Akonadi;
 *
 * class MyWindow : public KXmlGuiWindow
 * {
 *   public:
 *    MyWindow()
 *      : KXmlGuiWindow()
 *    {
 *      EntityTreeView *view = new EntityTreeView( this, this );
 *      setCentralWidget( view );
 *
 *      EntityTreeModel *model = new EntityTreeModel( ... );
 *      view->setModel( model );
 *    }
 * }
 *
 * @endcode
 *
 * @author Volker Krause <vkrause@kde.org>
 * @author Stephen Kelly <steveire@gmail.com>
 * @since 4.4
 */
class AKONADI_EXPORT EntityTreeView : public QTreeView
{
  Q_OBJECT

  public:
    /**
     * Creates a new entity tree view.
     *
     * @param parent The parent widget.
     */
    explicit EntityTreeView( QWidget *parent = 0 );

    /**
     * Creates a new entity tree view.
     *
     * @param xmlGuiClient The KXMLGUIClient the view is used in.
     *                     This is needed for the XMLGUI based context menu.
     *                     Passing 0 is ok and will disable the builtin context menu.
     * @param parent The parent widget.
     */
    explicit EntityTreeView( KXMLGUIClient *xmlGuiClient, QWidget *parent = 0 );

    /**
     * Destroys the entity tree view.
     */
    virtual ~EntityTreeView();

    /**
     * Sets the XML GUI client which the view is used in.
     *
     * This is needed if you want to use the built-in context menu.
     *
     * @param xmlGuiClient The KXMLGUIClient the view is used in.
     */
    void setXmlGuiClient( KXMLGUIClient *xmlGuiClient );

    /**
     * @reimplemented
     */
    virtual void setModel( QAbstractItemModel * model );

    /**
     * Sets whether to @p show the drop action menu on drop operation.
     */
    void setShowDropActionMenu( bool show );

    /**
     * Returns whether the drop action menu is shown on drop operation.
     */
    bool showDropActionMenu() const;

  Q_SIGNALS:
    /**
     * This signal is emitted whenever the user has clicked
     * a collection in the view.
     *
     * @param collection The clicked collection.
     */
    void clicked( const Akonadi::Collection &collection );

    /**
     * This signal is emitted whenever the user has clicked
     * an item in the view.
     *
     * @param item The clicked item.
     */
    void clicked( const Akonadi::Item &item );

    /**
     * This signal is emitted whenever the user has double clicked
     * a collection in the view.
     *
     * @param collection The double clicked collection.
     */
    void doubleClicked( const Akonadi::Collection &collection );

    /**
     * This signal is emitted whenever the user has double clicked
     * an item in the view.
     *
     * @param item The double clicked item.
     */
    void doubleClicked( const Akonadi::Item &item );

    /**
     * This signal is emitted whenever the current collection
     * in the view has changed.
     *
     * @param collection The new current collection.
     */
    void currentChanged( const Akonadi::Collection &collection );

    /**
     * This signal is emitted whenever the current item
     * in the view has changed.
     *
     * @param item The new current item.
     */
    void currentChanged( const Akonadi::Item &item );

  protected:
    using QTreeView::currentChanged;
    virtual void dragMoveEvent( QDragMoveEvent *event );
    virtual void timerEvent( QTimerEvent *event );
    virtual void dropEvent( QDropEvent *event );
    virtual void contextMenuEvent( QContextMenuEvent *event );
    virtual void startDrag( Qt::DropActions supportedActions );

  private:
    //@cond PRIVATE
    class Private;
    Private * const d;

    Q_PRIVATE_SLOT( d, void itemClicked( const QModelIndex& ) )
    Q_PRIVATE_SLOT( d, void itemDoubleClicked( const QModelIndex& ) )
    Q_PRIVATE_SLOT( d, void itemCurrentChanged( const QModelIndex& ) )
    Q_PRIVATE_SLOT( d, void slotSelectionChanged( const QItemSelection &, const QItemSelection & ) )
    //@endcond
};

}

#endif

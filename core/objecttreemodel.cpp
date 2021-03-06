/*
  objecttreemodel.cpp

  This file is part of GammaRay, the Qt application inspection and
  manipulation tool.

  Copyright (C) 2010-2011 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
  Author: Volker Krause <volker.krause@kdab.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "objecttreemodel.h"

#include "readorwritelocker.h"
#include "probe.h"

#include <QtCore/QEvent>
#include <QtCore/QThread>

#include <iostream>

#define IF_DEBUG(x)

extern void dumpObject(QObject *);

using namespace std;
using namespace GammaRay;

ObjectTreeModel::ObjectTreeModel(Probe *probe)
  : ObjectModelBase< QAbstractItemModel >(probe)
{
  connect(probe, SIGNAL(objectCreated(QObject*)),
          this, SLOT(objectAdded(QObject*)));
  connect(probe, SIGNAL(objectDestroyed(QObject*)),
          this, SLOT(objectRemoved(QObject*)));
  connect(probe, SIGNAL(objectReparanted(QObject*)),
          this, SLOT(objectReparanted(QObject*)));
}

void ObjectTreeModel::objectAdded(QObject *obj)
{
  // slot, hence should always land in main thread due to auto connection
  Q_ASSERT(thread() == QThread::currentThread());

  ReadOrWriteLocker objectLock(Probe::instance()->objectLock());
  if (!Probe::instance()->isValidObject(obj)) {
    IF_DEBUG(cout << "tree invalid obj added: " << hex << obj << endl;)
    return;
  }
  IF_DEBUG(cout << "tree obj added: " << hex << obj << " p: " << obj->parent() << endl;)
  Q_ASSERT(!obj->parent() || Probe::instance()->isValidObject(obj->parent()));

  if (indexForObject(obj).isValid()) {
    IF_DEBUG(cout << "tree double obj added: " << hex << obj << endl;)
    return;
  }

  // this is ugly, but apparently it can happen
  // that an object gets created without parent
  // then later the delayed signal comes in
  // so catch this gracefully by first adding the
  // parent if required
  if (obj->parent()) {
    const QModelIndex index = indexForObject(obj->parent());
    if (!index.isValid()) {
      IF_DEBUG(cout << "tree: handle parent first" << endl;)
      objectAdded(obj->parent());
    }
  }

  const QModelIndex index = indexForObject(obj->parent());

  // either we get a proper parent and hence valid index or there is no parent
  Q_ASSERT(index.isValid() || !obj->parent());

  QVector<QObject*> &children = m_parentChildMap[ obj->parent() ];

  beginInsertRows(index, children.size(), children.size());

  children.push_back(obj);
  m_childParentMap.insert(obj, obj->parent());

  endInsertRows();
}

void ObjectTreeModel::objectRemoved(QObject *obj)
{
  // slot, hence should always land in main thread due to auto connection
  Q_ASSERT(thread() == QThread::currentThread());

  IF_DEBUG(cout
           << "tree removed: "
           << hex << obj << " "
           << hex << obj->parent() << dec << " "
           << m_parentChildMap.value(obj->parent()).size() << " "
           << m_parentChildMap.contains(obj) << endl;)

  if (!m_childParentMap.contains(obj)) {
    Q_ASSERT(!m_parentChildMap.contains(obj));
    return;
  }

  QObject *parentObj = m_childParentMap[ obj ];
  const QModelIndex parentIndex = indexForObject(parentObj);
  if (parentObj && !parentIndex.isValid()) {
    return;
  }

  QVector<QObject*> &siblings = m_parentChildMap[ parentObj ];

  int index = siblings.indexOf(obj);

  if (index == -1) {
    return;
  }

  beginRemoveRows(parentIndex, index, index);

  siblings.remove(index);
  m_childParentMap.remove(obj);
  m_parentChildMap.remove(obj);

  endRemoveRows();
}

void ObjectTreeModel::objectReparanted(QObject *obj)
{
  // slot, hence should always land in main thread due to auto connection
  Q_ASSERT(thread() == QThread::currentThread());

  ReadOrWriteLocker objectLock(Probe::instance()->objectLock());
  if (Probe::instance()->isValidObject(obj)) {
    objectAdded(obj);
  }

  objectRemoved(obj);
}

QVariant ObjectTreeModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid()) {
    return QVariant();
  }

  QObject *obj = reinterpret_cast<QObject*>(index.internalPointer());

  ReadOrWriteLocker lock(Probe::instance()->objectLock());
  if (Probe::instance()->isValidObject(obj)) {
    return dataForObject(obj, index, role);
  } else if (role == Qt::DisplayRole) {
    if (index.column() == 0) {
      return Util::addressToString(obj);
    } else {
      return tr("<deleted>");
    }
  }

  return QVariant();
}

int ObjectTreeModel::rowCount(const QModelIndex &parent) const
{
  if (parent.column() == 1) {
    return 0;
  }
  QObject *parentObj = reinterpret_cast<QObject*>(parent.internalPointer());
  return m_parentChildMap.value(parentObj).size();
}

QModelIndex ObjectTreeModel::parent(const QModelIndex &child) const
{
  QObject *childObj = reinterpret_cast<QObject*>(child.internalPointer());
  return indexForObject(m_childParentMap.value(childObj));
}

QModelIndex ObjectTreeModel::index(int row, int column, const QModelIndex &parent) const
{
  QObject *parentObj = reinterpret_cast<QObject*>(parent.internalPointer());
  const QVector<QObject*> children = m_parentChildMap.value(parentObj);
  if (row < 0 || column < 0 || row >= children.size()  || column >= columnCount()) {
    return QModelIndex();
  }
  return createIndex(row, column, children.at(row));
}

QModelIndex ObjectTreeModel::indexForObject(QObject *object) const
{
  if (!object) {
    return QModelIndex();
  }
  QObject *parent = m_childParentMap.value(object);
  const QModelIndex parentIndex = indexForObject(parent);
  if (!parentIndex.isValid() && parent) {
    return QModelIndex();
  }
  int row = m_parentChildMap[ parent ].indexOf(object);
  if (row < 0) {
    return QModelIndex();
  }
  return index(row, 0, parentIndex);
}

#include "objecttreemodel.moc"

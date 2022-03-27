#include "stv_item_view.h"


StvItemView::StvItemView(QWidget *parent)
    : QTreeView(parent)
{}

void StvItemView::SetItemModel(StvItemModel *model)
{
	this->_model = model;
}

void StvItemView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
	this->QTreeView::selectionChanged(selected, deselected);

	if(selected.indexes().size() == 0)
		return;

	assert(selected.indexes().size() == 1);
	QStandardItem *item = this->_model->itemFromIndex(selected.indexes().front());
	if(item->type() == StvItemModel::SCENE)
		this->_model->SetSelectedScene(item);
}

void StvItemView::EditSelectedItem()
{
	this->edit(this->currentIndex());
}

//bool StvItemView::edit(const QModelIndex &index, EditTrigger trigger, QEvent *event)
//{
//	return QTreeView::edit(index, trigger, event);
//}

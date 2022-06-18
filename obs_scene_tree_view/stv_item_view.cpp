#include "obs_scene_tree_view/stv_item_view.h"

#include <QMouseEvent>
#include <util/config-file.h>


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
		this->_model->SetSelectedScene(item, obs_frontend_preview_program_mode_active());
}

void StvItemView::EditSelectedItem()
{
	this->edit(this->currentIndex());
}

void StvItemView::mouseDoubleClickEvent(QMouseEvent *event)
{
	if(obs_frontend_preview_enabled())
	{
		// If preview mode enabled, check whether the option to transition output scenes on double-click is active
		const bool transition_enabled = config_get_bool(obs_frontend_get_global_config(),
		                                                "BasicWindow", "TransitionOnDoubleClick");

		if(transition_enabled)
		{
			QStandardItem *item = this->_model->itemFromIndex(this->indexAt(event->pos()));
			if(item && item->type() == StvItemModel::SCENE)
			{
				this->_model->SetSelectedScene(item, false, true);
				return;
			}
		}
	}

	// If TransitionOnDoubleClick is disabled or a folder is selected, perform a normal edit on double click
	return QTreeView::mouseDoubleClickEvent(event);
}

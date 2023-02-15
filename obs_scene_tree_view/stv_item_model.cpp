#include "obs_scene_tree_view/stv_item_model.h"

#include <util/config-file.h>

#include <QMessageBox>
#include <QLineEdit>
#include <QMimeData>
#include <QRegularExpression>
#include <QtWidgets/QMainWindow>


StvFolderItem::StvFolderItem(const QString &text)
    : QStandardItem(text)
{
	this->setDropEnabled(true);

	QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
	QIcon icon = config_get_bool(obs_frontend_get_global_config(), "SceneTreeView", "ShowFolderIcons") ?
	            main_window->property("groupIcon").value<QIcon>() :
	            QIcon();
	this->setIcon(icon);
}

int StvFolderItem::type() const
{	return StvItemModel::FOLDER;	}


StvSceneItem::StvSceneItem(const QString &text, obs_weak_source_t *weak)
    : QStandardItem(text)
{
	this->setDropEnabled(false);
	this->setData(QVariant::fromValue(obs_weak_source_ptr({weak})), StvItemModel::OBS_SCENE);

	QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
	QIcon icon = config_get_bool(obs_frontend_get_global_config(), "SceneTreeView", "ShowSceneIcons") ?
	            main_window->property("sceneIcon").value<QIcon>() :
	            QIcon();
	this->setIcon(icon);
}

int StvSceneItem::type() const
{	return StvItemModel::SCENE;	}


StvItemModel::StvItemModel()
{}

StvItemModel::~StvItemModel()
{
	// Remove scene refs
	for(auto &scene : this->_scenes_in_tree)
	{
		obs_weak_source_release(scene.first);
	}

	this->_scenes_in_tree.clear();
}

QStringList StvItemModel::mimeTypes() const
{
	return QStringList(MIME_TYPE.data());
}

QMimeData *StvItemModel::mimeData(const QModelIndexList &indexes) const
{
	QMimeData *mime = new QMimeData();

	const int num_indexes = indexes.size();

	QByteArray mime_dat;
	mime_dat.reserve(num_indexes*sizeof(mime_item_data_t)+sizeof(int));
	mime_dat.append((const char*)&num_indexes, sizeof(int));

	for(const auto &index : indexes)
	{
		mime_item_data_t item_mime_dat;

		const QStandardItem *item = this->itemFromIndex(index);

		assert(item->type() == FOLDER || item->type() == SCENE);
		item_mime_dat.Type = (QITEM_TYPE)item->type();
		item_mime_dat.Data = item_mime_dat.Type == QITEM_TYPE::FOLDER ? (void*)item :
		                                                                (void*)item->data(OBS_SCENE).value<obs_weak_source_ptr>().ptr;

		mime_dat.append((const char*)&item_mime_dat, sizeof(mime_item_data_t));
	}

	mime->setData(MIME_TYPE.data(), mime_dat);

	return mime;
}

bool StvItemModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
	Q_UNUSED(action);
	Q_UNUSED(column);

	QStandardItem *parent_item = this->itemFromIndex(parent);
	if(!parent_item)
		parent_item = this->invisibleRootItem();
	else if(parent_item->type() == QITEM_TYPE::SCENE)
		return false;

	if(row < 0)
		row = 0;

	QByteArray qdat = data->data(MIME_TYPE.data());
	assert(qdat.size() >= (int)sizeof(int));

	const char *dat = qdat.constData();

	const int num_indexes = *(int*)dat;
	dat += sizeof(int);

	for(int i = 0; i < num_indexes; ++i)
	{
		// Find item and move it
		const mime_item_data_t *item_data = (const mime_item_data_t*)dat;
		assert(item_data->Type == FOLDER || item_data->Type == SCENE);
		if(item_data->Type == SCENE)
			this->MoveSceneItem((obs_weak_source_t*)item_data->Data, row, parent_item);
		else
			this->MoveSceneFolder((QStandardItem*)item_data->Data, row, parent_item);

		dat += sizeof(obs_weak_source_ptr);
	}

	return true;
}

void StvItemModel::UpdateTree(obs_frontend_source_list &scene_list, const QModelIndex &selected_index)
{
	this->UpdateSceneSize();

	source_map_t new_scene_tree;

	for (size_t i = 0; i < scene_list.sources.num; i++)
	{
		obs_source_t *source = scene_list.sources.array[i];
		assert(obs_scene_from_source(source) != nullptr);

		if(!this->IsManagedScene(source))
			continue;

		source_map_t::iterator scene_it;

		// Check if scene already in tree
		obs_weak_source_t *weak = obs_source_get_weak_source(source);

		scene_it = this->_scenes_in_tree.find(weak);
		if(scene_it != this->_scenes_in_tree.end())
		{
			// if already in tree, move to new set
			auto new_scene_it = new_scene_tree.emplace(scene_it->first, scene_it->second).first;
			this->_scenes_in_tree.erase(scene_it);
			scene_it = new_scene_it;

			obs_weak_source_release(weak);
		}
		else
		{
			// if not in tree, add it
			scene_it = new_scene_tree.emplace(weak, nullptr).first;
		}

		weak = nullptr;

		// Check that scene contains tree data
		obs_data_t *scene_dat = obs_source_get_private_settings(OBSGetStrongRef(scene_it->first).Get());

		if(!scene_it->second)
		{
			// Scene not yet in tree, add it at the correct position
			QStandardItem *selected = this->itemFromIndex(selected_index);
			QStandardItem *parent;
			if(selected)
			{
				assert(selected->type() == QITEM_TYPE::SCENE || selected->type() == QITEM_TYPE::FOLDER);

				if(selected->type() == QITEM_TYPE::FOLDER)
					parent = selected;
				else
					parent = this->GetParentOrRoot(selected->index());
			}
			else
			{
				selected = this->invisibleRootItem();
				parent = selected;
			}

			// Add new item to scene
			StvSceneItem *pItem = new StvSceneItem(obs_source_get_name(source), scene_it->first);

			const auto row = parent == selected ? 0 : selected->row();
			parent->insertRow(row, pItem);

			scene_it->second = pItem;
		}
		else
		{
			// Update scene name
			scene_it->second->setText(obs_source_get_name(source));
		}

		obs_data_release(scene_dat);
		scene_dat = nullptr;
	}

	// Erase all remaining elements in _scene_tree
	for(const auto &scene : this->_scenes_in_tree)
	{
		assert(scene.second);

		const int row = scene.second->row();
		this->removeRow(row, this->parent(scene.second->index()));

		// Remove scene reference
		obs_weak_source_release(scene.first);
	}

	this->_scenes_in_tree = std::move(new_scene_tree);
}

bool StvItemModel::CheckFolderNameUniqueness(const QString &name, QStandardItem *parent, QStandardItem *item_to_skip)
{
	const int row_count = parent->rowCount();
	for(int i=0; i<row_count; ++i)
	{
		QStandardItem *item = parent->child(i);
		if(item == item_to_skip)
			continue;

		if(item->type() == FOLDER && item->text() == name)
			return false;
	}

	return true;
}

void StvItemModel::SetSelectedScene(QStandardItem *item, bool set_preview_scene, bool force_set_scene)
{
	obs_weak_source_t *weak = item->data(QDATA_ROLE::OBS_SCENE).value<obs_weak_source_ptr>().ptr;
	OBSSourceAutoRelease source = OBSGetStrongRef(weak);
	if(source)
	{
		if(!set_preview_scene)
		{
			if(force_set_scene || OBSSourceAutoRelease(obs_frontend_get_current_scene()).Get() != source)
				obs_frontend_set_current_scene(source);
		}
		else if(force_set_scene || OBSSourceAutoRelease(obs_frontend_get_current_preview_scene()).Get() != source)
			obs_frontend_set_current_preview_scene(source);
	}
}

QStandardItem *StvItemModel::GetCurrentSceneItem()
{
	// Change source to the selected one
	OBSSourceAutoRelease source = this->GetCurrentScene();
	OBSWeakSource weak = OBSGetWeakRef(source);

	if(auto scene_it = this->_scenes_in_tree.find(weak); scene_it != this->_scenes_in_tree.end())
		return scene_it->second;
	else
	{
		blog(LOG_WARNING, "[%s] Couldn't find current scene in Scene Tree View", obs_module_name());
		return nullptr;
	}
}

OBSSourceAutoRelease StvItemModel::GetCurrentScene()
{
	return obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
}

void StvItemModel::SaveSceneTree(obs_data_t *root_folder_data, const char *scene_collection, QTreeView *view)
{
	OBSDataArrayAutoRelease folder_data = this->CreateFolderArray(*this->invisibleRootItem(), view);
	obs_data_set_array(root_folder_data, scene_collection, folder_data);
}

void StvItemModel::LoadSceneTree(obs_data_t *root_folder_data, const char *scene_collection, QTreeView *view)
{
	this->UpdateSceneSize();

	QStandardItem *root_item = this->invisibleRootItem();

	// Erase previous data
	this->CleanupSceneTree();

	// Add loaded data
	OBSDataArrayAutoRelease folder_array = obs_data_get_array(root_folder_data, scene_collection);
	if(folder_array)
	{
		std::list<StvFolderItem*> expandable_folders;
		this->LoadFolderArray(folder_array, *root_item, expandable_folders);

		for(auto &item : expandable_folders)
		{
			view->setExpanded(item->index(), true);
		}
	}
}

void StvItemModel::CleanupSceneTree()
{
	// Remove scene refs
	for(auto &scene : this->_scenes_in_tree)
	{
		obs_weak_source_release(scene.first);
	}

	this->_scenes_in_tree.clear();

	QStandardItem *root_item = this->invisibleRootItem();
	root_item->removeRows(0, root_item->rowCount());
}

QStandardItem *StvItemModel::GetParentOrRoot(const QModelIndex &index)
{
	QStandardItem *selected = this->itemFromIndex(this->parent(index));
	if(!selected)
		selected = this->invisibleRootItem();

	return selected;
}

QString StvItemModel::CreateUniqueFolderName(QStandardItem *folder_item, QStandardItem *parent)
{
	// Check that name is unique
	QString folder_name = folder_item->text();
	if(!this->CheckFolderNameUniqueness(folder_name, parent, folder_item))
	{
		QString format = folder_name.replace(QRegularExpression("\\d+$"), "%1");
		if(!format.endsWith("%1"))
			format += " %1";

		size_t i = 0;
		QString name;
		do
		{
			name = format.arg(QString::number(++i));
		}
		while(!this->CheckFolderNameUniqueness(name, parent, folder_item));

		folder_name = name;
	}

	return folder_name;
}

void StvItemModel::SetIconVisibility(bool enable_visibility, QITEM_TYPE item_type)
{
	if(item_type == SCENE)
		return this->SetSceneIconVisibility(enable_visibility);
	else
		return this->SetFolderIconVisibility(enable_visibility);
}

void StvItemModel::SetSceneIconVisibility(bool enable_visibility)
{
	QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
	QIcon icon = enable_visibility ? main_window->property("sceneIcon").value<QIcon>() : QIcon();

	return this->SetIcon(icon, SCENE, this->invisibleRootItem());
}

void StvItemModel::SetFolderIconVisibility(bool enable_visibility)
{
	QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
	QIcon icon = enable_visibility ? main_window->property("groupIcon").value<QIcon>() : QIcon();

	return this->SetIcon(icon, FOLDER, this->invisibleRootItem());
}

void StvItemModel::UpdateSceneSize()
{
	this->_scene_size.cx = config_get_int(obs_frontend_get_profile_config(), "Video", "BaseCX");
	this->_scene_size.cy = config_get_int(obs_frontend_get_profile_config(), "Video", "BaseCY");
}

bool StvItemModel::IsManagedScene(obs_scene_t *scene) const
{
	OBSSource source = obs_scene_get_source(scene);
	return this->IsManagedScene(source);
}

bool StvItemModel::IsManagedScene(obs_source_t *scene_source) const
{
	OBSDataAutoRelease settings = obs_source_get_settings(scene_source);
	return	obs_data_get_bool(settings, "custom_size") == false /*&&
			obs_data_get_bool(settings, "cx") == this->_scene_size.cx &&
					obs_data_get_bool(settings, "cy") == this->_scene_size.cy*/;
}

void StvItemModel::MoveSceneItem(obs_weak_source_t *source, int row, QStandardItem *parent_item)
{
	if(const auto scene_it = this->_scenes_in_tree.find(source); scene_it != this->_scenes_in_tree.end())
	{
		assert(scene_it->second->type() == SCENE);

		blog(LOG_INFO, "[%s] Moving %s", obs_module_name(), scene_it->second->text().toStdString().c_str());

		StvSceneItem *pItem = new StvSceneItem(scene_it->second->text(), scene_it->first);
		parent_item->insertRow(row, pItem);

		// Old item removed when returning true

		scene_it->second = pItem;
	}
	else
		blog(LOG_WARNING, "[%s] Couldn't find item to move in Scene Tree View", obs_module_name());
}

void StvItemModel::MoveSceneFolder(QStandardItem *item, int row, QStandardItem *parent_item)
{
	assert(item->type() == FOLDER);
	blog(LOG_INFO, "[%s] Moving %s", obs_module_name(), item->text().toStdString().c_str());

	// Check that name is unique
	QString new_name = this->CreateUniqueFolderName(item, parent_item);

	StvFolderItem *new_item = new StvFolderItem(new_name);
	parent_item->insertRow(row, new_item);

	for(int sub_row = 0; sub_row < item->rowCount(); ++sub_row)
	{
		QStandardItem *sub_item = item->child(sub_row);

		assert(sub_item->type() == FOLDER || sub_item->type() == SCENE);

		if(sub_item->type() == FOLDER)
			this->MoveSceneFolder(sub_item, sub_row, new_item);
		else
		{
			obs_weak_source_t *weak = sub_item->data(OBS_SCENE).value<obs_weak_source_ptr>().ptr;
			this->MoveSceneItem(weak, sub_row, new_item);
		}
	}
}

obs_data_array_t *StvItemModel::CreateFolderArray(QStandardItem &folder, QTreeView *view)
{
	obs_data_array_t *folder_data = obs_data_array_create();

	for(int i=0; i < folder.rowCount(); ++i)
	{
		QStandardItem *item = folder.child(i);
		assert(item->type() == FOLDER || item->type() == SCENE);

		OBSDataAutoRelease item_data = obs_data_create();
		if(item->type() == FOLDER)
		{
			OBSDataArrayAutoRelease sub_folder_data = this->CreateFolderArray(*item, view);
			obs_data_set_array(item_data, SCENE_TREE_CONFIG_FOLDER_DATA.data(), sub_folder_data);
			obs_data_set_bool(item_data, SCENE_TREE_CONFIG_FOLDER_EXPANDED.data(), view->isExpanded(item->index()));
			obs_data_set_string(item_data, SCENE_TREE_CONFIG_ITEM_NAME_DATA.data(), item->text().toStdString().c_str());
		}
		else
		{
			obs_weak_source_t *weak = item->data(QDATA_ROLE::OBS_SCENE).value<obs_weak_source_ptr>().ptr;
			OBSSourceAutoRelease source = OBSGetStrongRef(weak);
			obs_data_set_string(item_data, SCENE_TREE_CONFIG_ITEM_NAME_DATA.data(), obs_source_get_name(source));
		}

		obs_data_array_push_back(folder_data, item_data);
	}

	return folder_data;
}

void StvItemModel::LoadFolderArray(obs_data_array_t *folder_data, QStandardItem &folder, std::list<StvFolderItem*> &expandable_folders)
{
	const size_t item_count = obs_data_array_count(folder_data);
	for(size_t i=0; i < item_count; ++i)
	{
		OBSDataAutoRelease item_data = obs_data_array_item(folder_data, i);

		const char *item_name = obs_data_get_string(item_data, SCENE_TREE_CONFIG_ITEM_NAME_DATA.data());
		OBSDataArrayAutoRelease folder_data = obs_data_get_array(item_data, SCENE_TREE_CONFIG_FOLDER_DATA.data());

		// Check if this is folder or scene item (only folders have folder_data)
		if(!folder_data)
		{
			// Add scene to folder, skip if scene doesn't exist anymore
			OBSSceneAutoRelease scene = obs_get_scene_by_name(item_name);
			if(!scene || !this->IsManagedScene(scene))
				continue;

			{
				OBSSource source = obs_scene_get_source(scene);
				OBSWeakSource weak = obs_source_get_weak_source(source);

				// Skip if scene already in treeview
				// (see issue https://github.com/DigitOtter/obs_scene_tree_view/issues/19)
				if(this->_scenes_in_tree.find(weak) != this->_scenes_in_tree.end())
				{
					obs_weak_source_release(weak);
					continue;
				}

				StvSceneItem *new_scene_item = new StvSceneItem(item_name, weak);
				folder.appendRow(new_scene_item);

				this->_scenes_in_tree.emplace(weak, new_scene_item);
			}
		}
		else
		{
			StvFolderItem *new_folder_item = new StvFolderItem(item_name);
			this->LoadFolderArray(folder_data, *new_folder_item, expandable_folders);

			folder.appendRow(new_folder_item);

			// Check if folder should be expanded.
			// The folders are expanded after the tree is completely created to prevent new inserts from closing the folders again
			if(obs_data_get_bool(item_data, SCENE_TREE_CONFIG_FOLDER_EXPANDED.data()))
				expandable_folders.push_back(new_folder_item);
		}
	}
}

void StvItemModel::SetIcon(const QIcon &icon, QITEM_TYPE item_type, QStandardItem *item)
{
	if(!item)
		return;

	for(int i=0; i < item->rowCount(); ++i)
	{
		QStandardItem *child = item->child(i);
		if(child->type() == item_type)
			child->setIcon(icon);

		if(child->type() == FOLDER)
			this->SetIcon(icon, item_type, child);
	}
}

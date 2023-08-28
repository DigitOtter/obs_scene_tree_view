#include "obs_scene_tree_view/obs_scene_tree_view.h"

#include "obs_scene_tree_view/version.h"

#include <QLineEdit>
#include <QAction>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QWidgetAction>

#include <obs-module.h>
#include <util/platform.h>


OBS_DECLARE_MODULE();
OBS_MODULE_AUTHOR("DigitOtter");
OBS_MODULE_USE_DEFAULT_LOCALE(PROJECT_DATA_FOLDER, "en-US");

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("SceneTreeView");
}

bool obs_module_load()
{
	blog(LOG_INFO, "[%s] loaded version %s", obs_module_name(), PROJECT_VERSION);

	BPtr<char> stv_config_path = obs_module_config_path("");
	if(!os_mkdir(stv_config_path))
		blog(LOG_WARNING, "[%s] failed to create config dir '%s'", obs_module_name(), stv_config_path.Get());

	QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);
	obs_frontend_add_dock(new ObsSceneTreeView(main_window));
	obs_frontend_pop_ui_translation();

	return true;
}

MODULE_EXPORT void obs_module_unload()
{}

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()


ObsSceneTreeView::ObsSceneTreeView(QMainWindow *main_window)
    : QDockWidget(dynamic_cast<QWidget*>(main_window)),
      _add_scene_act(main_window->findChild<QAction*>("actionAddScene")),
      _remove_scene_act(main_window->findChild<QAction*>("actionRemoveScene")),
      _toggle_toolbars_scene_act(main_window->findChild<QAction*>("toggleListboxToolbars"))
{
	config_t *const global_config = obs_frontend_get_global_config();
	config_set_default_bool(global_config, "SceneTreeView", "ShowSceneIcons", false);
	config_set_default_bool(global_config, "SceneTreeView", "ShowFolderIcons", false);

	assert(this->_add_scene_act);
	assert(this->_remove_scene_act);

	this->_stv_dock.setupUi(this);
	this->hide();

	this->_stv_dock.stvTree->SetItemModel(&this->_scene_tree_items);
	this->_stv_dock.stvTree->setDefaultDropAction(Qt::DropAction::MoveAction);

	const bool show_icons = config_get_bool(global_config, "BasicWindow", "ShowListboxToolbars");
	this->on_toggleListboxToolbars(show_icons);

	// Add callback to obs scene list change event
	obs_frontend_add_event_callback(&ObsSceneTreeView::obs_frontend_event_cb, this);
	obs_frontend_add_save_callback(&ObsSceneTreeView::obs_frontend_save_cb, this);

	QObject::connect(this->_stv_dock.stvAdd, &QToolButton::released, this->_add_scene_act, &QAction::trigger);

	QObject::connect(this->_stv_dock.stvTree->itemDelegate(), SIGNAL(closeEditor(QWidget*,QAbstractItemDelegate::EndEditHint)),
	                 this, SLOT(on_SceneNameEdited(QWidget*)));
	                //main_window, SLOT(SceneNameEdited(QWidget*,QAbstractItemDelegate::EndEditHint)));

	QObject::connect(this->_toggle_toolbars_scene_act, &QAction::triggered, this, &ObsSceneTreeView::on_toggleListboxToolbars);

	this->_stv_dock.stvTree->setModel(&(this->_scene_tree_items));
}

ObsSceneTreeView::~ObsSceneTreeView()
{
	// Remove frontend cb
	obs_frontend_remove_save_callback(&ObsSceneTreeView::obs_frontend_save_cb, this);
	obs_frontend_remove_event_callback(&ObsSceneTreeView::obs_frontend_event_cb, this);
}

void ObsSceneTreeView::SaveSceneTree(const char *scene_collection)
{
	if(!scene_collection)
		return;

	BPtr<char> stv_config_file_path = obs_module_config_path(SCENE_TREE_CONFIG_FILE.data());

	OBSDataAutoRelease stv_data = obs_data_create_from_json_file(stv_config_file_path);
	if(!stv_data)
		stv_data = obs_data_create();

	this->_scene_tree_items.SaveSceneTree(stv_data, scene_collection, this->_stv_dock.stvTree);

	if(!obs_data_save_json(stv_data, stv_config_file_path))
		blog(LOG_WARNING, "[%s] Failed to save scene tree in '%s'", obs_module_name(), stv_config_file_path.Get());
}

void ObsSceneTreeView::LoadSceneTree(const char *scene_collection)
{
	assert(scene_collection);

	BPtr<char> stv_config_file_path = obs_module_config_path(SCENE_TREE_CONFIG_FILE.data());

	OBSDataAutoRelease stv_data = obs_data_create_from_json_file(stv_config_file_path);
	this->_scene_tree_items.LoadSceneTree(stv_data, scene_collection, this->_stv_dock.stvTree);
}

void ObsSceneTreeView::UpdateTreeView()
{
	obs_frontend_source_list scene_list = {};
	obs_frontend_get_scenes(&scene_list);

	this->_scene_tree_items.UpdateTree(scene_list, this->_stv_dock.stvTree->currentIndex());

	obs_frontend_source_list_free(&scene_list);

	this->SaveSceneTree(this->_scene_collection_name);
}

void ObsSceneTreeView::on_toggleListboxToolbars(bool visible)
{
	this->_stv_dock.listbox->setVisible(visible);
}

void ObsSceneTreeView::on_stvAddFolder_clicked()
{
	int row;
	QStandardItem *selected = this->_scene_tree_items.itemFromIndex(this->_stv_dock.stvTree->currentIndex());
	if(!selected)
	{
		selected = this->_scene_tree_items.invisibleRootItem();
		row = selected->rowCount();
	}
	else
	{
		if(selected->type() == StvItemModel::FOLDER)
			row = selected->rowCount();
		else
		{
			row = selected->row()+1;

			selected = this->_scene_tree_items.GetParentOrRoot(selected->index());
		}
	}

	// Get unique new folder name
	QString format{obs_module_text("SceneTreeView.DefaultFolderName")};
	int i = 0;
	QString new_folder_name = format.arg(i);
	OBSSourceAutoRelease source = nullptr;
	while(!this->_scene_tree_items.CheckFolderNameUniqueness(new_folder_name, selected))
	{
		new_folder_name = format.arg(++i);
	}

	StvFolderItem *pItem = new StvFolderItem(new_folder_name);
	selected->insertRow(row, pItem);

	this->SaveSceneTree(this->_scene_collection_name);
}

void ObsSceneTreeView::on_stvRemove_released()
{
	QStandardItem *selected = this->_scene_tree_items.itemFromIndex(this->_stv_dock.stvTree->currentIndex());
	if(selected)
	{
		assert(selected->type() == StvItemModel::FOLDER || selected->type() == StvItemModel::SCENE);
		if(selected->type() == StvItemModel::SCENE)
			QMetaObject::invokeMethod(this->_remove_scene_act, "triggered");
		else
			this->RemoveFolder(selected);
	}
}

void ObsSceneTreeView::on_stvTree_customContextMenuRequested(const QPoint &pos)
{
	QStandardItem *item = this->_scene_tree_items.itemFromIndex(this->_stv_dock.stvTree->indexAt(pos));

	QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());

	QMenu popup(this);
//	QMenu order(QTStr("Basic.MainMenu.Edit.Order"), this);

	popup.addAction(obs_module_text("SceneTreeView.AddScene"),
	                main_window, SLOT(on_actionAddScene_triggered()));

	popup.addAction(obs_module_text("SceneTreeView.AddFolder"),
	                this, SLOT(on_stvAddFolder_clicked()));

	if(item)
	{
		if(item->type() == StvItemModel::SCENE)
		{
			QAction *copyFilters = new QAction(QTStr("Copy.Filters"), this);
			copyFilters->setEnabled(false);
			connect(copyFilters, SIGNAL(triggered()),
			        main_window, SLOT(SceneCopyFilters()));
			QAction *pasteFilters = new QAction(QTStr("Paste.Filters"), this);
//			pasteFilters->setEnabled(
//			    !obs_weak_source_expired(copyFiltersSource));			// Cannot use (we can't check copyFiltersSource, as it's a private member of OBSBasic)
			connect(pasteFilters, SIGNAL(triggered()),
			        main_window, SLOT(ScenePasteFilters()));

			popup.addSeparator();
			popup.addAction(QTStr("Duplicate"),
			                main_window, SLOT(DuplicateSelectedScene()));
			popup.addAction(copyFilters);
			popup.addAction(pasteFilters);
			popup.addSeparator();
			QAction *rename = popup.addAction(QTStr("Rename"));
			QObject::connect(rename, SIGNAL(triggered()), this->_stv_dock.stvTree, SLOT(EditSelectedItem()));
			popup.addAction(QTStr("Remove"),
			                main_window, SLOT(RemoveSelectedScene()));
			popup.addSeparator();

//			order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveUp"),
//			                main_window, SLOT(on_actionSceneUp_triggered()));
//			order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveDown"),
//			        this, SLOT(on_actionSceneDown_triggered()));
//			order.addSeparator();

//			order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveToTop"),
//			        this, SLOT(MoveSceneToTop()));
//			order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveToBottom"),
//				    this, SLOT(MoveSceneToBottom()));
//			popup.addMenu(&order);

//			popup.addSeparator();

//			delete sceneProjectorMenu;
//			sceneProjectorMenu = new QMenu(QTStr("SceneProjector"));
//			AddProjectorMenuMonitors(sceneProjectorMenu, this,
//				         SLOT(OpenSceneProjector()));
//			popup.addMenu(sceneProjectorMenu);

			QAction *sceneWindow = popup.addAction(QTStr("SceneWindow"),
			                                       main_window, SLOT(OpenSceneWindow()));

			popup.addAction(sceneWindow);
			popup.addAction(QTStr("Screenshot.Scene"),
			                main_window, SLOT(ScreenshotScene()));
			popup.addSeparator();
			popup.addAction(QTStr("Filters"),
			                main_window, SLOT(OpenSceneFilters()));

			popup.addSeparator();

			this->_per_scene_transition_menu.reset(CreatePerSceneTransitionMenu(main_window));
			popup.addMenu(this->_per_scene_transition_menu.get());

			/* ---------------------- */

			QAction *multiviewAction = popup.addAction(QTStr("ShowInMultiview"));

			OBSSourceAutoRelease source = this->_scene_tree_items.GetCurrentScene();
			OBSDataAutoRelease data = obs_source_get_private_settings(source);

			obs_data_set_default_bool(data, "show_in_multiview", true);
			bool show = obs_data_get_bool(data, "show_in_multiview");

			multiviewAction->setCheckable(true);
			multiviewAction->setChecked(show);

			auto showInMultiview = [main_window](OBSData data) {
				bool show =
				    obs_data_get_bool(data, "show_in_multiview");
				obs_data_set_bool(data, "show_in_multiview", !show);
				// Workaround because OBSProjector::UpdateMultiviewProjectors() isn't available to modules
				QMetaObject::invokeMethod(main_window, "ScenesReordered");
			};

			connect(multiviewAction, &QAction::triggered,
			    std::bind(showInMultiview, data.Get()));

			copyFilters->setEnabled(obs_source_filter_count(source) > 0);
		}

		popup.addSeparator();

		// Enable/disable scene or folder icon
		const auto toggleName = item->type() == StvItemModel::SCENE ? obs_module_text("SceneTreeView.ToggleSceneIcons") :
		                                                              obs_module_text("SceneTreeView.ToggleFolderIcons");

		QAction *toggleIconAction = popup.addAction(toggleName);
		toggleIconAction->setCheckable(true);

		const auto configName = item->type() == StvItemModel::SCENE ? "ShowSceneIcons" : "ShowFolderIcons";
		const bool showIcon = config_get_bool(obs_frontend_get_global_config(), "SceneTreeView", configName);

		toggleIconAction->setChecked(showIcon);

		auto toggleIcon = [this, showIcon, configName, item]() {
			config_set_bool(obs_frontend_get_global_config(), "SceneTreeView", configName, !showIcon);
			this->_scene_tree_items.SetIconVisibility(!showIcon, (StvItemModel::QITEM_TYPE)item->type());
		};

		connect(toggleIconAction, &QAction::triggered, toggleIcon);
	}

//	popup.addSeparator();

//	bool grid = ui->scenes->GetGridMode();

//	QAction *gridAction = new QAction(grid ? QTStr("Basic.Main.ListMode")
//	                       : QTStr("Basic.Main.GridMode"),
//	                  this);
//	connect(gridAction, SIGNAL(triggered()), this,
//	    SLOT(GridActionClicked()));
//	popup.addAction(gridAction);

	popup.exec(QCursor::pos());
}

void ObsSceneTreeView::on_SceneNameEdited(QWidget *editor)
{
	QStandardItem *selected = this->_scene_tree_items.itemFromIndex(this->_stv_dock.stvTree->currentIndex());
	if(selected->type() == StvItemModel::SCENE)
	{
		QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
		QMetaObject::invokeMethod(main_window, "SceneNameEdited", Q_ARG(QWidget*, editor));
	}
	else
	{
		QLineEdit *edit = qobject_cast<QLineEdit *>(editor);
		std::string text = QT_TO_UTF8(edit->text().trimmed());

		selected->setText(this->_scene_tree_items.CreateUniqueFolderName(selected,
		                                                                 this->_scene_tree_items.GetParentOrRoot(selected->index())));
	}
}

void ObsSceneTreeView::SelectCurrentScene()
{
	QStandardItem *item = this->_scene_tree_items.GetCurrentSceneItem();
	if(item && item->index() != this->_stv_dock.stvTree->currentIndex())
		QMetaObject::invokeMethod(this->_stv_dock.stvTree, "setCurrentIndex", Q_ARG(QModelIndex, item->index()));
}

void ObsSceneTreeView::RemoveFolder(QStandardItem *folder)
{
	int row = 0;
	int row_count = folder->rowCount();
	while(row < row_count)
	{
		QStandardItem *item = folder->child(row);
		assert(item->type() == StvItemModel::FOLDER || item->type() == StvItemModel::SCENE);

		if(item->type() == StvItemModel::SCENE)
		{
			// Keep source reference to prevent race conditions on deletion via "triggered action"
			obs_weak_source_t *weak = item->data(StvItemModel::OBS_SCENE).value<obs_weak_source_ptr>().ptr;
			OBSSourceAutoRelease source = OBSGetStrongRef(weak);

			this->_scene_tree_items.SetSelectedScene(item, obs_frontend_preview_program_mode_active());
			QMetaObject::invokeMethod(this->_remove_scene_act, "triggered");
		}
		else
			this->RemoveFolder(item);

		// Check if item was deleted. If not, move to next row
		if(row_count == folder->rowCount())
			++row;

		row_count = folder->rowCount();
	}

	// Remove folder if empty
	if(folder->rowCount() == 0)
		this->_scene_tree_items.GetParentOrRoot(folder->index())->removeRow(folder->row());
}

Q_DECLARE_METATYPE(OBSSource);

static inline OBSSource GetTransitionComboItem(QComboBox *combo, int idx)
{
	return combo->itemData(idx).value<OBSSource>();
}

QMenu *ObsSceneTreeView::CreatePerSceneTransitionMenu(QMainWindow *main_window)
{
	OBSSourceAutoRelease scene = this->_scene_tree_items.GetCurrentScene();
	QMenu *menu = new QMenu(QTStr("TransitionOverride"));
	QAction *action;

	OBSDataAutoRelease data = obs_source_get_private_settings(scene);

	obs_data_set_default_int(data, "transition_duration", 300);

	const char *curTransition = obs_data_get_string(data, "transition");
	int curDuration = (int)obs_data_get_int(data, "transition_duration");

	QSpinBox *duration = new QSpinBox(menu);
	duration->setMinimum(50);
	duration->setSuffix(" ms");
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setValue(curDuration);

	// Workaround to get the transitions menu from the main menu
	QComboBox *combo = main_window->findChild<QComboBox*>("transitions");
	assert(combo);

	auto setTransition = [this, combo](QAction *action) {
		int idx = action->property("transition_index").toInt();
		OBSSourceAutoRelease scene = this->_scene_tree_items.GetCurrentScene();
		OBSDataAutoRelease data =
		    obs_source_get_private_settings(scene);

		if (idx == -1) {
			obs_data_set_string(data, "transition", "");
			return;
		}

		OBSSource tr = GetTransitionComboItem(combo, idx);

		if (tr)
		{
			const char *name = obs_source_get_name(tr);
			obs_data_set_string(data, "transition", name);
		}
	};

	auto setDuration = [this](int duration) {
		OBSSourceAutoRelease scene = this->_scene_tree_items.GetCurrentScene();
		OBSDataAutoRelease data =
		    obs_source_get_private_settings(scene);

		obs_data_set_int(data, "transition_duration", duration);
	};

	connect(duration, (void (QSpinBox::*)(int)) & QSpinBox::valueChanged, setDuration);

	std::string none = "None";
	for (int i = -1; i < combo->count(); i++)
	{
		const char *name = "";

		if (i >= 0)
		{
			OBSSource tr;
			tr = GetTransitionComboItem(combo, i);
			if (!tr)
				continue;
			name = obs_source_get_name(tr);
		}

		bool match = (name && strcmp(name, curTransition) == 0);

		if (!name || !*name)
			name = none.c_str();

		action = menu->addAction(QT_UTF8(name));
		action->setProperty("transition_index", i);
		action->setCheckable(true);
		action->setChecked(match);

		connect(action, &QAction::triggered,
		    std::bind(setTransition, action));
	}

	QWidgetAction *durationAction = new QWidgetAction(menu);
	durationAction->setDefaultWidget(duration);

	menu->addSeparator();
	menu->addAction(durationAction);
	return menu;
}

void ObsSceneTreeView::ObsFrontendEvent(enum obs_frontend_event event)
{
	// Update our tree view when scene list was changed
	if(event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
	{
		this->_scene_collection_name = obs_frontend_get_current_scene_collection();

		// Load saved scene locations, then add any missing items that weren't saved
		this->LoadSceneTree(this->_scene_collection_name);
		this->UpdateTreeView();

		this->SelectCurrentScene();

		// We're updating the icons here to allow the main_window to load themes first
		// Set icons, force style sheet recalculation. Taken from obs source code, qt-wrappers.cpp, setThemeID()
		QMainWindow *main_window = reinterpret_cast<QMainWindow*>(obs_frontend_get_main_window());
		this->_stv_dock.stvAdd->setIcon(this->_add_scene_act->icon());
		this->_stv_dock.stvRemove->setIcon(this->_remove_scene_act->icon());
		this->_stv_dock.stvAddFolder->setIcon(main_window->property("groupIcon").value<QIcon>());

		QString qss = this->styleSheet();
		this->setStyleSheet("/* */");
		this->setStyleSheet(qss);
	}
	else if(event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED)
		this->UpdateTreeView();
	else if(event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED)
		this->SelectCurrentScene();
	else if(event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP)
	{
		this->_scene_tree_items.CleanupSceneTree();
		this->_scene_collection_name = nullptr;
	}
	else if(event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING)
		this->SaveSceneTree(this->_scene_collection_name);
	else if(event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
	{
		this->_scene_collection_name = obs_frontend_get_current_scene_collection();
		this->LoadSceneTree(this->_scene_collection_name);
		this->UpdateTreeView();
	}
	else if(event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED)
	{
		// TODO: Delete old scene tree from json file

		this->_scene_collection_name = obs_frontend_get_current_scene_collection();
		this->SaveSceneTree(this->_scene_collection_name);

		this->UpdateTreeView();
	}
}

void ObsSceneTreeView::ObsFrontendSave(obs_data_t */*save_data*/, bool saving)
{
	if(saving)
		this->SaveSceneTree(this->_scene_collection_name);
}

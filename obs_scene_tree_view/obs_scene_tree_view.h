#ifndef OBS_SCENE_TREE_VIEW_H
#define OBS_SCENE_TREE_VIEW_H

#include <map>

#include <QAbstractItemDelegate>
#include <QtWidgets/QDockWidget>

#include <util/util.hpp>

#include "obs-data.h"
#include "obs_scene_tree_view/stv_item_model.h"
#include "ui_scene_tree_view.h"

class ObsSceneTreeView
        : public QDockWidget
{
		Q_OBJECT

	public:
		static constexpr std::string_view SCENE_TREE_CONFIG_FILE = "scene_tree.json";

		ObsSceneTreeView(QMainWindow *main_window);
		virtual ~ObsSceneTreeView() override;

		void SaveSceneTree(const char *scene_collection);
		void LoadSceneTree(const char *scene_collection);

	protected slots:
		void UpdateTreeView();

		void on_toggleListboxToolbars(bool visible);

		void on_stvAddFolder_clicked();
		void on_stvRemove_released();

		// Copied from OBS, OBSBasic::on_scenes_customContextMenuRequested()
		void on_stvTree_customContextMenuRequested(const QPoint &pos);

		void on_SceneNameEdited(QWidget *editor);

	private:
		QAction *_add_scene_act = nullptr;
		QAction *_remove_scene_act = nullptr;
		QAction *_toggle_toolbars_scene_act = nullptr;

		std::unique_ptr<QMenu> _per_scene_transition_menu;

		Ui::STVDock _stv_dock;

		StvItemModel _scene_tree_items;
		BPtr<char> _scene_collection_name = nullptr;

		void SelectCurrentScene();
		void RemoveFolder(QStandardItem *folder);

		// Copied from OBS, OBSBasic::CreatePerSceneTransitionMenu()
		QMenu *CreatePerSceneTransitionMenu(QMainWindow *main_window);

		inline static void obs_frontend_event_cb(enum obs_frontend_event event, void *private_data)
		{	((ObsSceneTreeView*)private_data)->ObsFrontendEvent(event);	}

		inline static void obs_frontend_save_cb(obs_data_t *save_data, bool saving, void *private_data)
		{	((ObsSceneTreeView*)private_data)->ObsFrontendSave(save_data, saving);	}

		void ObsFrontendEvent(enum obs_frontend_event event);
		void ObsFrontendSave(obs_data_t *save_data, bool saving);
};

#endif //OBS_SCENE_TREE_VIEW_H

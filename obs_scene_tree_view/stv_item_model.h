#ifndef STV_ITEM_MODEL_H
#define STV_ITEM_MODEL_H

#include <obs.hpp>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QStandardItemModel>

#include <string_view>


struct obs_weak_source_ptr
{
		obs_weak_source_t *ptr;
};

Q_DECLARE_METATYPE(obs_weak_source_ptr);

class StvFolderItem
        : public QStandardItem
{
	public:
		StvFolderItem(const QString &text);
		virtual ~StvFolderItem() override = default;
		int type() const override;
};

class StvSceneItem
        : public QStandardItem
{
	public:
		StvSceneItem(const QString &text, obs_weak_source_t *weak);
		virtual ~StvSceneItem() override = default;
		int type() const override;
};


class StvItemModel
        : public QStandardItemModel
{
		Q_OBJECT

		static constexpr std::string_view MIME_TYPE = "application/x-stvindexlist";
		static constexpr std::string_view SCENE_TREE_CONFIG_FOLDER_DATA = "folder";
		static constexpr std::string_view SCENE_TREE_CONFIG_ITEM_NAME_DATA = "name";

	public:
		enum QDATA_ROLE
		{	OBS_SCENE = Qt::UserRole	};

		enum QITEM_TYPE
		{	FOLDER = QStandardItem::UserType+1, SCENE	};

		StvItemModel();
		virtual ~StvItemModel() override;

		QStringList mimeTypes() const override;
		QMimeData *mimeData(const QModelIndexList &indexes) const override;
		bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

		void UpdateTree(obs_frontend_source_list &scene_list, const QModelIndex &selected_index);

		bool CheckFolderNameUniqueness(const QString &name, QStandardItem *parent, QStandardItem *item_to_skip = nullptr);

		void SetSelectedScene(QStandardItem *item);
		QStandardItem *GetCurrentSceneItem();
		OBSSourceAutoRelease GetCurrentScene();

		void SaveSceneTree(obs_data_t *root_folder_data);
		void LoadSceneTree(obs_data_t *root_folder_data);

		QStandardItem *GetParentOrRoot(const QModelIndex &index);

	protected slots:
		void on_itemChanged(QStandardItem *item);

	private:
		struct mime_item_data_t
		{
			QITEM_TYPE Type;
			void *Data;			// Either QStandardItem* (if Type == FOLDER) or obs_weak_source_t* (if Type == SCENE)
		};

		struct SceneComp
		{
			bool operator() (obs_weak_source_t *x, obs_weak_source_t *y) const
			{	return OBSGetStrongRef(x).Get() < OBSGetStrongRef(y).Get();	}
		};
		using source_map_t = std::map<obs_weak_source_t*, QStandardItem*, SceneComp>;

		source_map_t _scenes_in_tree;

		void MoveSceneItem(obs_weak_source_t *source, int row, QStandardItem *parent_item);
		void MoveSceneFolder(QStandardItem *item, int row, QStandardItem *parent_item);

		obs_data_array_t *CreateFolderArray(QStandardItem &folder);
		void LoadFolderArray(obs_data_array_t *folder_data, QStandardItem &folder);

		QString CreateUniqueFolderName(QStandardItem *folder_item, QStandardItem *parent);

		friend class StvFolderItem;
		friend class StvSceneItem;
};

#endif // STV_ITEM_MODEL_H

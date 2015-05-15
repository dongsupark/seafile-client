#include "nautilus-seafile.h"
#include <libnautilus-extension/nautilus-menu-provider.h>
#include <libnautilus-extension/nautilus-column-provider.h>
#include <libnautilus-extension/nautilus-info-provider.h>
#include <gtk/gtk.h>
#include "seafile-rpc-client.h"
#include <seafile/seafile.h>
#include <seafile/seafile-object.h>
#include <string.h>

static guint kAutoconnectInterval = 10 * 1000;
static guint kUpdateWatchSetInterval = 4 * 1000;
static guint kUpdateFileStatusInterval = 2 * 1000;
static void start_autoconnect_rpc_client ();
static GObject* find_repo (const char* path);
static gboolean update_file_status_by_file (SearpcClient *client, NautilusFileInfo *file);
static void clean_up_all_data ();

static GList *watch_set_ = NULL;
static GHashTable *file_status_ = NULL;

/* used on start up only */
static void initialize_work ();

static GList *seafile_extension_get_background_items (NautilusMenuProvider *provider, GtkWidget *window, NautilusFileInfo *file_info)
{
    return NULL;
}

static GList *seafile_extension_get_file_items (NautilusMenuProvider *provider, GtkWidget *window, GList *files)
{
    /* TODO implement */
    return NULL;
}

static GList *seafile_extension_get_columns(NautilusColumnProvider *provider)
{
    NautilusColumn *column;
    GList *ret;
    column = nautilus_column_new ("SeafileExtension::sync_status_column",
                                  "SeafileExtension::sync_status",
                                  "Sync Status",
                                  "Sync Status Description");
    ret = g_list_append(NULL, column);

    return ret;
}

static NautilusOperationResult seafile_extension_update_file_info (NautilusInfoProvider     *provider,
                                                                   NautilusFileInfo         *file,
                                                                   GClosure                 *update_complete,
                                                                   NautilusOperationHandle **handle)
{
    char *uri = NULL;
    gchar *filename = NULL;
    SearpcClient *client;
    GObject *repo;

    /* there func are not used for we handle this in a synchronize way for simplicity */
    (void)update_complete; /* unused */
    (void)handle; /* unused */

    client = seafile_rpc_get_instance ();
    g_return_val_if_fail (client, NAUTILUS_OPERATION_COMPLETE);
    g_return_val_if_fail (file, NAUTILUS_OPERATION_FAILED);

    uri = nautilus_file_info_get_uri (file);
    filename = g_filename_from_uri (uri, NULL, NULL);
    if (!filename)
    {
        g_free (uri);
        return NAUTILUS_OPERATION_COMPLETE;
    }

    repo = find_repo (filename);
    if (!repo)
    {
        g_free (filename);
        g_free (uri);
        return NAUTILUS_OPERATION_COMPLETE;
    }
    /* we can change it to a asynchronize way if we want? */
    update_file_status_by_file (client, file);

    g_free (filename);
    g_free (uri);

    return NAUTILUS_OPERATION_COMPLETE;
}

static void seafile_extension_cancel_update (NautilusInfoProvider     *provider,
                                             NautilusOperationHandle  *handle)
{
    /* Nothing */
}

static void
seafile_extension_menu_provider_iface_init (NautilusMenuProviderIface *iface)
{
    iface->get_background_items = seafile_extension_get_background_items;
    iface->get_file_items = seafile_extension_get_file_items;
}

static void
seafile_extension_column_provider_iface_init (NautilusColumnProviderIface *iface)
{
    iface->get_columns = seafile_extension_get_columns;
    return;
}

static void
seafile_extension_info_provider_iface_init (NautilusInfoProviderIface *iface)
{
    iface->update_file_info = seafile_extension_update_file_info;
    iface->cancel_update = seafile_extension_cancel_update;
}

static void
seafile_extension_instance_init (SeafileExtension *seafile)
{
}

static void
seafile_extension_class_init (SeafileExtensionClass *class)
{
}

static GType seafile_type = 0;

GType
seafile_extension_get_type (void)
{
	return seafile_type;
}

void
seafile_extension_register_type (GTypeModule *module)
{
    static const GTypeInfo info = {
                    sizeof (SeafileExtensionClass),
                    (GBaseInitFunc) NULL,
                    (GBaseFinalizeFunc) NULL,
                    (GClassInitFunc) seafile_extension_class_init,
                    NULL,
                    NULL,
                    sizeof (SeafileExtension),
                    0,
                    (GInstanceInitFunc) seafile_extension_instance_init,
                    NULL
            };

    static const GInterfaceInfo menu_provider_iface_info = {
        (GInterfaceInitFunc) seafile_extension_menu_provider_iface_init,
        NULL,
        NULL,
    };

    static const GInterfaceInfo column_provider_iface_info = {
        (GInterfaceInitFunc) seafile_extension_column_provider_iface_init,
        NULL,
        NULL
    };

    static const GInterfaceInfo info_provider_iface_info = {
        (GInterfaceInitFunc) seafile_extension_info_provider_iface_init,
        NULL,
        NULL,
    };

    seafile_type = g_type_module_register_type (module, G_TYPE_OBJECT, "seafileExtension", &info, 0);

    g_type_module_add_interface (module,
                     seafile_type,
                     NAUTILUS_TYPE_MENU_PROVIDER,
                     &menu_provider_iface_info);

    g_type_module_add_interface (module,
                                 seafile_type,
                                 NAUTILUS_TYPE_COLUMN_PROVIDER,
                                 &column_provider_iface_info);

    g_type_module_add_interface (module,
                     seafile_type,
                     NAUTILUS_TYPE_INFO_PROVIDER,
                     &info_provider_iface_info);

    initialize_work ();
}

static gboolean update_file_status (gpointer data);
static gboolean is_file_status_started = FALSE;
static void stop_update_file_status (gpointer data)
{
    /* we get disconnected */
    is_file_status_started = FALSE;
}

static gboolean update_file_status (gpointer data);
static void start_update_file_status ()
{
    g_return_if_fail (!is_file_status_started);
    is_file_status_started = TRUE;
    g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, kUpdateFileStatusInterval, update_file_status, NULL, stop_update_file_status);
}

static gboolean is_watch_set_started = FALSE;
static void stop_update_watch_set (gpointer data)
{
    is_watch_set_started = FALSE;
    /* we get disconnected */
    clean_up_all_data ();
    start_autoconnect_rpc_client ();
}

static gboolean update_watch_set (gpointer data);
static void start_update_watch_set ()
{
    g_return_if_fail (!is_watch_set_started);
    is_watch_set_started = TRUE;
    g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, kUpdateWatchSetInterval, update_watch_set, NULL, stop_update_watch_set);
    start_update_file_status ();
}

static gboolean is_connect_rpc_started = FALSE;
static void stop_connect_rpc_client (gpointer data)
{
    is_connect_rpc_started = FALSE;

    start_update_watch_set ();
}

static gboolean connect_rpc_client (gpointer data);
static void start_autoconnect_rpc_client ()
{
    seafile_rpc_instance_disconnect ();

    g_return_if_fail (!is_connect_rpc_started);
    is_connect_rpc_started = TRUE;
    g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, kAutoconnectInterval, connect_rpc_client, NULL, stop_connect_rpc_client);
}

static void initialize_work ()
{
    file_status_ = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    if (connect_rpc_client (NULL))
    {
        start_autoconnect_rpc_client ();
    }
    else
    {
        stop_connect_rpc_client (NULL);
    }
}

static gboolean connect_rpc_client (gpointer data)
{
    /* connected? */
    g_return_val_if_fail (!seafile_rpc_instance_connect (), FALSE);

    /* should we retry connect */
    return TRUE;
}

static __inline gboolean update_file_status_by_name (SearpcClient *client, const char* path)
{
    gboolean retval;
    GFile *gfile;
    NautilusFileInfo *file;
    gfile = g_file_new_for_path (path);
    g_return_val_if_fail (gfile, TRUE);
    file = nautilus_file_info_lookup (gfile);
    if (!file)
    {
        g_object_unref (gfile);
        return TRUE;
    }
    retval = update_file_status_by_file (client, file);

    g_object_unref (file);
    g_object_unref (gfile);
    return retval;
}

static __inline void set_nautilus_file_info (NautilusFileInfo *file, const char *status)
{
    char *old_status = nautilus_file_info_get_string_attribute (file, "SeafileExtension::sync_status");
    if (old_status && strcmp (old_status, status) == 0)
    {
        g_free (old_status);
        return;
    }
    if (old_status)
    {
        nautilus_file_info_invalidate_extension_info (file);
    }
    if (strlen(status) == 0)
    {
        nautilus_file_info_add_emblem (file, "");
    }
    else
    {
        char* emblem_name = g_strconcat ("seafile-", status, NULL);
        nautilus_file_info_add_emblem (file, emblem_name);
        g_free (emblem_name);
    }

    nautilus_file_info_add_string_attribute (file, "SeafileExtension::sync_status", status);

    g_free (old_status);
}

static gboolean update_file_status_by_file (SearpcClient *client, NautilusFileInfo *file)
{
    char *uri = NULL;
    char* worktree;
    char* repo_id;
    GObject *repo;
    const char* path_in_repo;
    GError *error = NULL;
    char *status;
    gchar *filename;

    uri = nautilus_file_info_get_uri (file);
    filename = g_filename_from_uri (uri, NULL, NULL);
    if (!filename)
    {
        g_free (uri);
        return TRUE;
    }

    repo = find_repo (filename);
    if (!repo)
    {
        g_free (filename);
        g_free (uri);
        return TRUE;
    }
    g_object_get (repo, "worktree", &worktree,
                        "id", &repo_id, NULL);
    path_in_repo = filename + strlen (worktree);

    /* skip root diectory and trailing "/" */
    if (*path_in_repo++ == '\0')
    {
        g_free (worktree);
        g_free (repo_id);
        g_free (filename);
        g_free (uri);
        return TRUE;
    }

    /* we can change it to a asynchronize way if we want? */
    status = searpc_client_call__string (
        client,
        "seafile_get_path_sync_status",
        &error, 3,
        "string", repo_id,
        "string", path_in_repo,
        "int", nautilus_file_info_is_directory (file));

    if (error)
    {
        gboolean is_disconnected = TRANSPORT_ERROR_CODE == error->code;
        g_error_free (error);
        g_free (worktree);
        g_free (repo_id);
        g_free (filename);
        g_free (uri);
        return !is_disconnected;
    }

    g_hash_table_replace (file_status_, filename, status);

    set_nautilus_file_info (file, status);

    nautilus_file_info_add_string_attribute (file,
                             "SeafileExtension::sync_status",
                             status);

    g_free (worktree);
    g_free (repo_id);
    g_free (uri);
    return TRUE;
}

static gboolean update_file_status (gpointer data)
{
    SearpcClient *client;
    GHashTableIter iter;
    gpointer key, value;

    /* we handle this in a synchronize way for simplicity */

    client = seafile_rpc_get_instance ();
    g_return_val_if_fail (client, FALSE);

    g_hash_table_iter_init (&iter, file_status_);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        g_return_val_if_fail (update_file_status_by_name (client, (const char*)key), FALSE);
    }

    return TRUE;
}

static __inline const char* get_path_status_from_state(const char *state)
{
    int len = strlen (state);
#define COMPARE_EQUAL(x) (strncmp (state, x, len) == 0)
    if (COMPARE_EQUAL("disabled"))
        return "unknown"; /* SYNC_STATE_DISABLED */
    else if (COMPARE_EQUAL("relay not connected") || COMPARE_EQUAL("waiting for sync") || COMPARE_EQUAL("relay authenticating"))
        return "synced"; /* SYNC_STATE_WAITING */
    else if (COMPARE_EQUAL("initializing") || COMPARE_EQUAL("cancel pending"))
        return "synced"; /* SYNC_STATE_INIT */
    else if (COMPARE_EQUAL("downloading") || COMPARE_EQUAL("uploading") || COMPARE_EQUAL("merging"))
        return "syncing"; /* SYNC_STATE_ING */
    else if (COMPARE_EQUAL("synchronized"))
        return "synced"; /* SYNC_STATE_DONE */
    else if (COMPARE_EQUAL("error"))
        return "error"; /* SYNC_STATE_ERROR */
#undef COMPARE_EQUAL
    return "";
}

static void update_file_status_for_repo (GObject *repo, const char *status_hint, SearpcClient *client)
{
    char *worktree;
    char *repo_id;
    GFile *gfile;
    NautilusFileInfo *file;
    g_object_get (repo, "worktree", &worktree, "id", &repo_id, NULL);

    if (!status_hint)
    {
        GError *error = NULL;
        SeafileSyncTask *task = (SeafileSyncTask *)
            searpc_client_call__object (client,
                                        "seafile_get_repo_sync_task",
                                        SEAFILE_TYPE_SYNC_TASK,
                                        &error, 1,
                                        "string", repo_id);
        if (error)
        {
            status_hint = "error";
            g_error_free (error);
        }
        else
        {
            char *state = NULL;
            g_object_get(task, "state", &state, NULL);
            status_hint = get_path_status_from_state(state);
            g_free (state);
            g_object_unref(task);
        }
    }

    gfile = g_file_new_for_path (worktree);
    g_return_if_fail (gfile);
    file = nautilus_file_info_lookup (gfile);
    if (!file)
    {
        g_object_unref (gfile);

        g_free (repo_id);
        g_free (worktree);
        return;
    }
    set_nautilus_file_info (file, status_hint);

    g_object_unref (file);
    g_object_unref (gfile);

    g_free (repo_id);
    g_free (worktree);
}

static gboolean update_watch_set (gpointer data)
{
    GError *error = NULL;
    GList *repos;
    SearpcClient *client = seafile_rpc_get_instance ();
    (void)data; /* unused */

    g_return_val_if_fail (client, FALSE);

    repos = seafile_get_repo_list (client, 0, 0, &error);
    if (error != NULL)
    {
        gboolean is_disconnected = TRANSPORT_ERROR_CODE == error->code;
        g_error_free(error);
        /* call stop_update_watch_set when return */
        /* TODO invalidate all old icons! */
        return !is_disconnected;
    }
    if (watch_set_)
    {
        /* TODO cleanup all old icons! */
        g_list_foreach (watch_set_, (GFunc)g_object_unref, NULL);
        g_list_free (watch_set_);
    }
    watch_set_ = repos;
    if (watch_set_)
    {
        GList *head = watch_set_;
        while (head)
        {
            update_file_status_for_repo (head->data, NULL, client);
            head = head->next;
        }
    }
    return TRUE;
}

static GObject* find_repo (const char* path)
{
    GList *head = watch_set_;

    g_return_val_if_fail (watch_set_, NULL);
    while (head)
    {
        char* worktree;
        int len;
        g_object_get (head->data, "worktree", &worktree, NULL);
        len = strlen(worktree);
        if (strncmp (worktree, path, len) == 0) {
            g_free (worktree);
            break;
        }
        g_free (worktree);

        head = g_list_next(head);
    }
    return head ? head->data : NULL;
}

static void clean_up_all_data ()
{
    GHashTableIter iter;
    gpointer key, value;

    /* clean up file info */
    GFile *gfile;
    NautilusFileInfo *file;
    g_hash_table_iter_init (&iter, file_status_);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        gfile = g_file_new_for_path ((const char*)key);
        if (!gfile)
        {
            continue;
        }
        file = nautilus_file_info_lookup (gfile);
        if (!file)
        {
            g_object_unref (gfile);
            continue;
        }
        nautilus_file_info_invalidate_extension_info (file);
        g_object_unref (file);
        g_object_unref (gfile);
    }
    g_hash_table_remove_all (file_status_);

    /* clean up watch set */
    if (watch_set_)
    {
        GList *head = watch_set_;
        while (head)
        {
            GObject *repo = head->data;
            update_file_status_for_repo (repo, "none", NULL);
            g_object_unref (head->data);
            head = head->next;
        }
        g_list_free (watch_set_);
        watch_set_ = NULL;
    }
}


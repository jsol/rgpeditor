#include <adwaita.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "editor_page.h"

// static GHashTable *entries;

/*
TODO
- Implement codeblock?
- Implement sub headers / bold / italic
- Scroll page list
*/

static GtkWindow *app_window;

static gchar *
get_current_ws(void)
{
  GError *lerr = NULL;
  gchar *save_file;
  gchar *path = NULL;

  save_file = g_build_filename(g_getenv("HOME"), ".rpg_editor", NULL);

  if (!g_file_get_contents(save_file, &path, NULL, &lerr)) {
    g_warning("Could not load workspace path: %s",
              lerr->message != NULL ? lerr->message : "No error message");
  }

  g_free(save_file);

  return path;
}

static void
save_current_ws(const gchar *path)
{
  GError *lerr = NULL;
  gchar *save_file;

  if (path == NULL) {
    return;
  }

  save_file = g_build_filename(g_getenv("HOME"), ".rpg_editor", NULL);

  if (!g_file_set_contents(save_file, path, -1, &lerr)) {
    g_warning("Could not save workspace path: %s",
              lerr->message != NULL ? lerr->message : "No error message");
  }

  g_free(save_file);
}

static void
anchors_foreach(gpointer data, gpointer user_data)
{
  GtkTextView *view = GTK_TEXT_VIEW(user_data);
  GtkTextChildAnchor *anchor = GTK_TEXT_CHILD_ANCHOR(data);
  EditorPage *target;

  if (gtk_text_child_anchor_get_deleted(anchor)) {
    return;
  }

  target = g_object_get_data(G_OBJECT(anchor), "target");

  gtk_text_view_add_child_at_anchor(view, editor_page_in_content_button(target),
                                    anchor);
}

static void
header_changed(GtkEditable *self, gpointer user_data)
{
  EditorPage *page = EDITOR_PAGE(user_data);

  g_object_set(page, "heading", gtk_editable_get_text(self), NULL);
}

static void
add_style(G_GNUC_UNUSED gpointer key, gpointer val, gpointer user_data)
{
  GString *style = (GString *) user_data;
  EditorPage *page = EDITOR_PAGE(val);

  g_print("Creating style %s\n", page->css_name);
  g_string_append_printf(style, ".%s {background-color: %s;}", page->css_name,
                         gdk_rgba_to_string(&page->color));
}

static void
update_css(GHashTable *pages)
{
  GdkDisplay *display;
  GtkCssProvider *provider;
  GString *style = g_string_new(".in-text-button {padding: 0px; margin: 0px;  "
                                "margin-bottom: -8px;}");

  g_hash_table_foreach(pages, add_style, style);

  display = gdk_display_get_default();

  provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, style->str);

  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_USER);

  g_string_free(style, TRUE);
}

static void
color_changed(GtkColorDialogButton *self,
              G_GNUC_UNUSED gpointer,
              gpointer user_data)
{
  EditorPage *page = EDITOR_PAGE(user_data);

  const GdkRGBA *color;

  color = gtk_color_dialog_button_get_rgba(self);

  page->color.red = color->red;
  page->color.green = color->green;
  page->color.blue = color->blue;
  page->color.alpha = color->alpha;

  update_css(page->pages);

  g_print("Color change\n");
}

static void
remove_choice_cb(G_GNUC_UNUSED GObject *source_object,
                 G_GNUC_UNUSED GAsyncResult *res,
                 G_GNUC_UNUSED gpointer data)
{
}

static void
remove_page(G_GNUC_UNUSED GObject *button, EditorPage *self)
{
  GtkAlertDialog *dia;
  const gchar *buttons[3] = { "Yes", "No", NULL };
  g_print("Creating dialog\n");

  dia = gtk_alert_dialog_new("Are you certain that you wish to remove page %s",
                             self->heading);

  gtk_alert_dialog_set_buttons(dia, buttons);
  gtk_alert_dialog_set_cancel_button(dia, 1);
  gtk_alert_dialog_set_default_button(dia, 1);
  gtk_alert_dialog_set_modal(dia, TRUE);

  gtk_alert_dialog_choose(dia, app_window, NULL, remove_choice_cb, self);
}

static void
set_page(EditorPage *page, GtkApplication *app)
{
  GtkWidget *content_header;
  GtkWidget *textarea;
  GtkWidget *color_picker;
  EditorPage *current_page;
  GtkWidget *remove_button;

  content_header = g_object_get_data(G_OBJECT(app), "content_header");
  textarea = g_object_get_data(G_OBJECT(app), "textarea");
  current_page = g_object_get_data(G_OBJECT(app), "current_page");
  color_picker = g_object_get_data(G_OBJECT(app), "color_picker");
  remove_button = g_object_get_data(G_OBJECT(app), "remove_button");

  if (current_page == page) {
    return;
  }
  g_signal_handlers_disconnect_matched(color_picker, G_SIGNAL_MATCH_FUNC, 0, 0,
                                       NULL, color_changed, NULL);
  g_signal_handlers_disconnect_matched(content_header, G_SIGNAL_MATCH_FUNC, 0,
                                       0, NULL, header_changed, NULL);
  g_signal_handlers_disconnect_matched(remove_button, G_SIGNAL_MATCH_FUNC, 0, 0,
                                       NULL, remove_page, NULL);

  gtk_text_view_set_buffer(GTK_TEXT_VIEW(textarea), page->content);

  gtk_editable_set_text(GTK_EDITABLE(content_header), page->heading);

  gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(color_picker),
                                   &page->color);

  g_ptr_array_foreach(page->anchors, anchors_foreach, textarea);

  g_signal_connect(color_picker, "notify::rgba", G_CALLBACK(color_changed),
                   page);
  g_signal_connect(content_header, "changed", G_CALLBACK(header_changed), page);
  g_signal_connect(remove_button, "clicked", G_CALLBACK(remove_page), page);

  g_object_set_data(G_OBJECT(app), "current_page", page);
  g_print("Set current Page %p , app %p\n", page, app);
}

static void
single_anchor(EditorPage *page,
              GtkTextChildAnchor *anchor,
              GtkWidget *button,
              GObject *app)
{
  GtkTextView *textarea;
  EditorPage *current_page;

  current_page = g_object_get_data(app, "current_page");
  g_print("Page %p vs %p, app %p\n", page, current_page, app);
  if (page != current_page) {
    g_print("Bailing due to not active page\n");
    return;
  }

  textarea = g_object_get_data(app, "textarea");

  gtk_text_view_add_child_at_anchor(textarea, button, anchor);
}

static void
set_heading(G_GNUC_UNUSED GObject *button, GObject *app)
{
  EditorPage *current_page;

  current_page = g_object_get_data(app, "current_page");

  if (current_page == NULL) {
    return;
  }
  editor_page_selected_to_heading(current_page);
}

static GdkContentProvider *
on_drag_prepare(GtkDragSource *source, double x, double y, GtkButton *self)
{
  return gdk_content_provider_new_typed(GTK_TYPE_BUTTON, self);
}

static void
on_drag_begin(GtkDragSource *source, GdkDrag *drag, GtkButton *self)
{
  // Set the widget as the drag icon
  GdkPaintable *paintable = gtk_widget_paintable_new(GTK_WIDGET(self));
  gtk_drag_source_set_icon(source, paintable, 0, 0);
  g_object_unref(paintable);
}

static gboolean
on_drop(GtkDropTarget *target,
        const GValue *value,
        double x,
        double y,
        gpointer data)
{
  GtkButton *target_button = data;
  GtkButton *dropped_button = NULL;
  GQueue *pages_list;
  GtkBox *box;
  gpointer dropped_page;
  gconstpointer target_page;

  box = g_object_get_data(G_OBJECT(target), "pages_box");
  pages_list = g_object_get_data(G_OBJECT(target), "pages_list");
  dropped_button = g_value_get_object(value);

  dropped_page = g_object_get_data(G_OBJECT(dropped_button), "page");
  target_page = g_object_get_data(G_OBJECT(target_button), "page");

  gtk_box_reorder_child_after(box, GTK_WIDGET(dropped_button),
                              GTK_WIDGET(target_button));
  gtk_box_reorder_child_after(box, GTK_WIDGET(target_button),
                              GTK_WIDGET(dropped_button));

  g_queue_remove(pages_list, dropped_page);

  GList *link = g_queue_find(pages_list, target_page);
  g_queue_insert_before(pages_list, link, dropped_page);

  g_print("Sorting buttons with %p after %p: \n", dropped_button, target_button);

  return TRUE;
}

static void
page_created(EditorPage *page, GObject *app)
{
  GtkBox *pages_box;
  GQueue *pages_list;
  GtkDragSource *drag_source;
  GtkDropTarget *drop_target;

  pages_box = g_object_get_data(app, "pages_box");
  pages_list = g_object_get_data(app, "pages_list");

  drag_source = gtk_drag_source_new();
  drop_target = gtk_drop_target_new(GTK_TYPE_BUTTON, GDK_ACTION_COPY);

  g_object_set_data(G_OBJECT(drop_target), "pages_box", pages_box);

  g_object_set_data(G_OBJECT(drop_target), "pages_box", pages_box);
  g_object_set_data(G_OBJECT(drop_target), "pages_list", pages_list);

  g_signal_connect(drag_source, "prepare", G_CALLBACK(on_drag_prepare),
                   page->page_button);
  g_signal_connect(drag_source, "drag-begin", G_CALLBACK(on_drag_begin),
                   page->page_button);
  g_signal_connect(drop_target, "drop", G_CALLBACK(on_drop), page->page_button);

  gtk_widget_add_controller(GTK_WIDGET(page->page_button),
                            GTK_EVENT_CONTROLLER(drag_source));
  gtk_widget_add_controller(GTK_WIDGET(page->page_button),
                            GTK_EVENT_CONTROLLER(drop_target));

  gtk_box_append(pages_box, page->page_button);
  g_queue_push_tail(pages_list, page);

  g_signal_connect(page, "switch-page", G_CALLBACK(set_page), app);

  g_signal_connect(page, "new-anchor", G_CALLBACK(single_anchor), app);

  update_css(page->pages);
  g_print("Page created: %s\n", page->heading);
}

static gboolean
prepare_folder(const gchar *base_path)
{
  GDir *dir;
  gchar *content = NULL;
  gchar *meta_name;
  gchar **rows;

  if (base_path == NULL) {
    return FALSE;
  }

  /* OK if we either have a meta.tab file or if we have an empty folder.
   * Remove all existing files if there is a meta.tab file
   */

  meta_name = g_build_filename(base_path, "meta.tab", NULL);
  if (g_file_get_contents(meta_name, &content, NULL, NULL)) {
    rows = g_strsplit(content, "\n", -1);

    for (gint i = 0; rows[i] != NULL; i++) {
      gchar **meta;
      gchar *filename;

      meta = g_strsplit(rows[i], "\t", 2);

      if (meta[0] == NULL || !g_str_has_suffix(meta[0], ".md")) {
        g_strfreev(meta);
        continue;
      }

      filename = g_build_filename(base_path, meta[0], NULL);

      g_unlink(filename);

      g_free(filename);
      g_strfreev(meta);
    }

    g_free(meta_name);
    g_strfreev(rows);
    g_free(content);

    return TRUE;
  }

  dir = g_dir_open(base_path, 0, NULL);

  /* No meta file */
  if (g_dir_read_name(dir) != NULL) {
    g_dir_close(dir);
    return FALSE;
  }
  g_dir_close(dir);
  return TRUE;
}

static void
save(GtkApplication *app, const gchar *base_path)
{
  GQueue *pages_list;
  GString *meta;
  gchar *meta_path;
  GError *lerr = NULL;
  gchar *root;

  if (base_path == NULL) {
    root = (gchar *) g_object_get_data(G_OBJECT(app), "save-path");
    g_print("USING OLD BASE PATH: %s\n",
            (gchar *) g_object_get_data(G_OBJECT(app), "save-path"));
  } else {
    root = (gchar *) base_path;
  }

  if (!prepare_folder(root)) {
    g_warning("Can not save to %s", base_path);
    return;
  }

  g_print("Before Saving file: %s\n", root);
  if (base_path != NULL) {
    g_object_set_data_full(G_OBJECT(app), "save-path", g_strdup(base_path),
                           g_free);
  }

  pages_list = g_object_get_data(G_OBJECT(app), "pages_list");
  meta = g_string_new("");

  for (GList *iter = pages_list->head; iter != NULL; iter = iter->next) {
    EditorPage *page = EDITOR_PAGE(iter->data);
    gchar *name;
    gchar *file;
    gchar *full_path;
    GString *content;

    name = g_str_to_ascii(page->heading, NULL);
    file = g_strdup_printf("%s.md", name);
    full_path = g_build_filename(root, file, NULL);

    g_print("Saving file... %s + %s -> %s -> %s\n", root, name, file, full_path);

    g_string_append_printf(meta, "%s\t%s\n", file,
                           gdk_rgba_to_string(&page->color));

    content = editor_page_to_md(page);

    if (!g_file_set_contents(full_path, content->str, content->len, &lerr)) {
      g_warning("Could not save %s: %s", full_path, lerr->message);
      g_clear_error(&lerr);
    }

    g_free(name);
    g_free(file);
    g_free(full_path);
    g_string_free(content, TRUE);
  }

  meta_path = g_build_filename(root, "meta.tab", NULL);

  if (!g_file_set_contents(meta_path, meta->str, meta->len, &lerr)) {
    g_warning("Could not save %s: %s", meta_path, lerr->message);
    g_clear_error(&lerr);
  }

  save_current_ws(root);

  g_free(meta_path);
  g_string_free(meta, TRUE);
}

static void
pages_load_iter(G_GNUC_UNUSED gpointer key,
                gpointer value,
                G_GNUC_UNUSED gpointer user_data)
{
  g_assert(value);

  editor_page_fix_content(value);
}

static void
load_repo(const gchar *name, GHashTable *pages, GtkApplication *app)
{
  gchar *content = NULL;
  gchar *meta_name;
  GError *lerr = NULL;
  gchar **rows;

  g_message("Loading name: %s", name);

  g_object_set_data_full(G_OBJECT(app), "save-path", g_strdup(name), g_free);

  g_message("Saved name as: %s",
            (gchar *) g_object_get_data(G_OBJECT(app), "save-path"));

  meta_name = g_build_filename(name, "meta.tab", NULL);
  if (!g_file_get_contents(meta_name, &content, NULL, &lerr)) {
    g_warning("Could not open meta file for %s! %s", name, lerr->message);
    g_clear_error(&lerr);
    g_free(meta_name);
    return;
  }

  rows = g_strsplit(content, "\n", -1);

  for (gint i = 0; rows[i] != NULL; i++) {
    gchar **meta;
    gchar *filename;
    EditorPage *page;
    GdkRGBA color;

    meta = g_strsplit(rows[i], "\t", 2);

    if (meta[0] == NULL || !g_str_has_suffix(meta[0], ".md")) {
      g_strfreev(meta);
      continue;
    }

    filename = g_build_filename(name, meta[0], NULL);

    gdk_rgba_parse(&color, meta[1]);

    page = editor_page_load(pages, filename, &color, G_CALLBACK(page_created),
                            app);

    if (i == 0) {
      set_page(page, app);
    }

    g_free(filename);
    g_strfreev(meta);
  }

  g_hash_table_foreach(pages, pages_load_iter, NULL);

  update_css(pages);

  g_free(meta_name);
  g_strfreev(rows);
  g_free(content);
}

static void
open_file_cb(GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  GError *lerr = NULL;
  GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(
                                                       source_object),
                                                     res, &lerr);

  if (file == NULL) {
    g_warning("Error opening file: %s",
              lerr != NULL ? lerr->message : "no error message");
  } else {
    load_repo(g_file_peek_path(file), g_hash_table_new(g_str_hash, g_str_equal),
              app);
    g_clear_object(&file);
  }
}

static void
save_file_cb(GObject *source_object, GAsyncResult *res, gpointer data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  GError *lerr = NULL;
  GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(
                                                       source_object),
                                                     res, &lerr);

  if (file == NULL) {
    g_warning("Error saving file: %s",
              lerr != NULL ? lerr->message : "no error message");
  } else {
    save(app, g_file_peek_path(file));
    g_clear_object(&file);
  }
}

static void
open_menu_cb(GSimpleAction *simple_action, GVariant *parameter, gpointer *data)
{
  g_print("Hello\n");
  GtkFileDialog *dialog = gtk_file_dialog_new();

  gtk_file_dialog_select_folder(dialog, app_window, NULL, open_file_cb, data);
}

static void
save_menu_cb(GSimpleAction *simple_action, GVariant *parameter, gpointer *data)
{
  g_print("Hello\n");
  GtkFileDialog *dialog = gtk_file_dialog_new();

  gtk_file_dialog_select_folder(dialog, app_window, NULL, save_file_cb, data);
}

static void
new_menu_cb(GSimpleAction *simple_action, GVariant *parameter, gpointer *data)
{
  GtkApplication *app = GTK_APPLICATION(data);
  EditorPage *page;
  g_print("Setting new page!");

  page = editor_page_new("Overview", g_hash_table_new(g_str_hash, g_str_equal),
                         NULL, G_CALLBACK(page_created), app);

  set_page(page, app);
}

static void
event_key_released(G_GNUC_UNUSED GtkEventController *self,
                   guint keyval,
                   G_GNUC_UNUSED guint keycode,
                   GdkModifierType state,
                   gpointer user_data)

{
  GtkApplication *app = GTK_APPLICATION(user_data);

  if (keyval == 115 && (state & GDK_CONTROL_MASK)) {
    /* ctrl+s */

    gchar *save_path = g_object_get_data(G_OBJECT(app), "save-path");
    if (save_path == NULL) {
      save_menu_cb(NULL, NULL, user_data);
    } else {
      save(app, NULL);
    }
  } else if (keyval == 98 && (state & GDK_CONTROL_MASK)) {
    /* ctrl + b*/
    set_heading(NULL, G_OBJECT(app));
  }
}

static void
build_menu(GtkWidget *header, GtkApplication *app)
{
  GtkWidget *menu_button = gtk_menu_button_new();
  GMenu *menubar = g_menu_new();
  GMenuItem *menu_item_menu;

  menu_item_menu = g_menu_item_new("New", "app.new");
  g_menu_append_item(menubar, menu_item_menu);
  g_object_unref(menu_item_menu);

  menu_item_menu = g_menu_item_new("Save", "app.save");
  g_menu_append_item(menubar, menu_item_menu);
  g_object_unref(menu_item_menu);

  menu_item_menu = g_menu_item_new("Open", "app.open");
  g_menu_append_item(menubar, menu_item_menu);
  g_object_unref(menu_item_menu);

  GSimpleAction *act_open = g_simple_action_new("open", NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_open));
  g_signal_connect(act_open, "activate", G_CALLBACK(open_menu_cb), app);

  GSimpleAction *act_save = g_simple_action_new("save", NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_save));
  g_signal_connect(act_save, "activate", G_CALLBACK(save_menu_cb), app);

  GSimpleAction *act_new = g_simple_action_new("new", NULL);
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_new));
  g_signal_connect(act_new, "activate", G_CALLBACK(new_menu_cb), app);

  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button),
                                 G_MENU_MODEL(menubar));
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);
}

static void
activate(GtkApplication *app, gpointer user_data)
{
  GtkWidget *window;
  GtkWidget *textarea;

  GtkWidget *box;
  GtkWidget *splitbar;
  GtkWidget *pages_box;
  GtkWidget *content_box;
  GtkWidget *content_header_box;
  GtkWidget *content_header;
  GtkWidget *color_picker;
  GtkWidget *heading_button;
  GtkWidget *remove_button;
  GtkWidget *scroll;
  GtkEventController *event_controller;
  // EditorPage *page;

  pages_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

  textarea = gtk_text_view_new();

  scroll = gtk_scrolled_window_new();

  splitbar = adw_overlay_split_view_new();

  adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(splitbar),
                                     pages_box);

  content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  content_header = gtk_editable_label_new("");
  gtk_widget_add_css_class(content_header, "title-1");
  gtk_widget_set_margin_start(content_box, 20);

  heading_button = gtk_button_new_with_label("Bold");
  gtk_widget_set_halign(heading_button, GTK_ALIGN_END);

  color_picker = gtk_color_dialog_button_new(gtk_color_dialog_new());
  gtk_widget_set_halign(color_picker, GTK_ALIGN_END);

  remove_button = gtk_button_new_from_icon_name("edit-delete");
  gtk_widget_set_halign(remove_button, GTK_ALIGN_END);

  content_header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
  gtk_widget_set_hexpand(content_header_box, TRUE);
  gtk_widget_set_hexpand(content_header, TRUE);

  gtk_box_append(GTK_BOX(content_header_box), content_header);
  gtk_box_append(GTK_BOX(content_header_box), heading_button);
  gtk_box_append(GTK_BOX(content_header_box), color_picker);
  gtk_box_append(GTK_BOX(content_header_box), remove_button);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textarea);
  gtk_box_append(GTK_BOX(content_box), content_header_box);
  gtk_box_append(GTK_BOX(content_box), scroll);

  adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(splitbar),
                                     content_box);

  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(textarea), 20);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textarea), GTK_WRAP_WORD);

  window = adw_application_window_new(app);
  GtkWidget *title = adw_window_title_new("Editor", NULL);
  GtkWidget *header = adw_header_bar_new();
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
  build_menu(header, app);

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_box_append(GTK_BOX(box), header);
  gtk_box_append(GTK_BOX(box), splitbar);
  GQueue *pages_list = g_queue_new();

  g_object_set_data(G_OBJECT(app), "content_header", content_header);
  g_object_set_data(G_OBJECT(app), "textarea", textarea);
  g_object_set_data(G_OBJECT(app), "pages_box", pages_box);
  g_object_set_data(G_OBJECT(app), "pages_list", pages_list);
  g_object_set_data(G_OBJECT(app), "color_picker", color_picker);
  g_object_set_data(G_OBJECT(app), "remove_button", remove_button);
  g_object_set_data(G_OBJECT(textarea), "app", app);

  // page = editor_page_new("Overview", g_hash_table_new(g_str_hash,
  // g_str_equal),
  //                      G_CALLBACK(page_created), app);

  /* set_page(page, app); */

  gchar *saved_path = get_current_ws();

  if (saved_path != NULL && strlen(saved_path) > 3) {
    g_message("Loading pages from %s", saved_path);
    load_repo(saved_path, g_hash_table_new(g_str_hash, g_str_equal), app);
  }

  g_free(saved_path);

  g_signal_connect(heading_button, "clicked", G_CALLBACK(set_heading), app);

  event_controller = gtk_event_controller_key_new();
  g_signal_connect(event_controller, "key-released",
                   G_CALLBACK(event_key_released), app);
  gtk_widget_add_controller(window, event_controller);

  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), box);

  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),
                                                   TRUE);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 300);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 400);

  gtk_window_set_default_size(GTK_WINDOW(window), 1200, 720);
  gtk_window_present(GTK_WINDOW(window));
  app_window = GTK_WINDOW(window);

  gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(window), TRUE);
}

int
main(int argc, char *argv[])
{
  AdwApplication *app;

  app = adw_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_application_run(G_APPLICATION(app), argc, argv);

  g_object_unref(app);

  return 0;
}

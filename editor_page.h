#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/** Public variables. Move to .c file to make private */
struct _EditorPage {
  GObject parent;

  gchar *heading;
  GtkTextBuffer *content;
  GtkWidget *page_button;
  GPtrArray *anchors;
  GPtrArray *buttons;

  gchar *css_name;
  GdkRGBA color;

  GHashTable *pages;
  GCallback created_cb;
  gpointer user_data;

  GtkTextTag *bold;
};

/*
 * Type declaration.
 */

#define EDITOR_TYPE_PAGE editor_page_get_type()
G_DECLARE_FINAL_TYPE(EditorPage, editor_page, EDITOR, PAGE, GObject)

/*
 * Method definitions.
 */
EditorPage *editor_page_new(const gchar *heading,
                            GHashTable *pages,
                            GdkRGBA *color,
                            GCallback created_cb,
                            gpointer user_data);

GtkWidget *editor_page_in_content_button(EditorPage *self);

GString *editor_page_to_md(EditorPage *self);

EditorPage *editor_page_load(GHashTable *pages,
                             gchar *filename,
                             GdkRGBA *color,
                             GCallback created_cb,
                             gpointer user_data);
void editor_page_fix_content(EditorPage *page);

void editor_page_selected_to_heading(EditorPage *self);

G_END_DECLS

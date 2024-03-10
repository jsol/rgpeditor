#include "editor_page.h"
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

G_DEFINE_TYPE(EditorPage, editor_page, G_TYPE_OBJECT)

typedef enum {
  PROP_HEADING = 1,
  PROP_CONTENT,
  PROP_PAGE_BUTTON,
  N_PROPERTIES
} EditorPageProperty;

static GParamSpec *obj_properties[N_PROPERTIES] = {
  NULL,
};

enum editor_page_signals {
  EDITOR_PAGE_SWITCH = 0,
  EDITOR_PAGE_NEW_ANCHOR,
  EDITOR_PAGE_LAST
};

static guint editor_signals[EDITOR_PAGE_LAST] = { 0 };

typedef void (*create_cb)(gpointer, gpointer);

struct add_link_ctx {
  GtkTextMark *start_mark;
  GtkTextMark *stop_mark;
  EditorPage *page;
};

static gboolean
validate_name(const gchar *name)
{
  const gchar *iter;

  g_assert(name);

  if (!g_str_has_prefix(name, "[[") || !g_str_has_suffix(name, "]")) {
    return FALSE;
  }

  iter = name + 2;
  while (iter[0] != ']') {
    gunichar utf_c;

    utf_c = g_utf8_get_char(iter);

    if (!g_unichar_isprint(utf_c)) {
      return FALSE;
    }

    if (g_unichar_isspace(utf_c) && iter[0] != ' ') {
      return FALSE;
    }

    iter = g_utf8_next_char(iter);
  }

  return TRUE;
}

static gboolean
validate_name_only(const gchar *name)
{
  g_assert(name);

  while (name[0] != '\0') {
    gunichar utf_c;

    utf_c = g_utf8_get_char(name);

    if (!g_unichar_isprint(utf_c)) {
      return FALSE;
    }

    if (g_unichar_isspace(utf_c) && name[0] != ' ') {
      return FALSE;
    }

    name = g_utf8_next_char(name);
  }

  return TRUE;
}

static void
add_link_anchor(gpointer user_data)
{
  struct add_link_ctx *ctx = (struct add_link_ctx *) user_data;
  EditorPage *other;
  GtkTextBuffer *buffer;
  GtkTextIter start, end;
  // GtkWidget *label;
  gchar *name;
  GtkTextChildAnchor *anchor;
  GtkWidget *button;

  buffer = ctx->page->content;
  /*
  start_mark = gtk_text_buffer_get_mark(buffer, "start-new-link");
  stop_mark = gtk_text_buffer_get_mark(buffer, "stop-new-link");
*/

  gtk_text_buffer_get_iter_at_mark(buffer, &start, ctx->start_mark);
  gtk_text_buffer_get_iter_at_mark(buffer, &end, ctx->stop_mark);

  name = gtk_text_iter_get_text(&start, &end);
  g_print("ADD_LINK NAME %s\n", name);

  gtk_text_iter_backward_char(&start);
  gtk_text_iter_backward_char(&start);
  gtk_text_iter_forward_char(&end);
  gtk_text_iter_forward_char(&end);
  gtk_text_buffer_delete(buffer, &start, &end);

  anchor = gtk_text_buffer_create_child_anchor(buffer, &start);

  other = g_hash_table_lookup(ctx->page->pages, name);

  if (!other) {
    other = editor_page_new(name, ctx->page->pages, NULL, ctx->page->created_cb,
                            ctx->page->user_data);
  }
  g_object_set_data(G_OBJECT(anchor), "target", other);

  g_ptr_array_add(ctx->page->anchors, g_object_ref(anchor));

  // gtk_text_view_add_child_at_anchor(textarea, widget, anchor);
  /* EMIT new anchor */
  button = editor_page_in_content_button(other);
  g_object_set_data(G_OBJECT(button), "anchor", anchor);
  g_object_set_data(G_OBJECT(button), "target", ctx->page);

  g_signal_emit(ctx->page, editor_signals[EDITOR_PAGE_NEW_ANCHOR], 0, anchor,
                button);

  g_free(name);
  g_free(ctx);
}

static void
insert_text(GtkTextBuffer *self,
            const GtkTextIter *location,
            gchar *text,
            gint len,
            gpointer user_data)
{
  static gchar last = ' ';
  EditorPage *page = EDITOR_PAGE(user_data);

  if (len != 1) {
    return;
  }

  if (text[0] == '[' && last == '[') {
    g_debug("Start link");
    last = ' ';

    gtk_text_buffer_create_mark(self, "start-link", location, TRUE);
    return;
  }

  if (text[0] == ']' && last == ']') {
    GtkTextMark *start = gtk_text_buffer_get_mark(self, "start-link");

    if (start != NULL) {
      GtkTextIter start_location;

      gtk_text_buffer_get_iter_at_mark(self, &start_location, start);
      (void) gtk_text_iter_backward_char(&start_location);
      gchar *name = gtk_text_iter_get_text(&start_location, location);

      if (validate_name(name)) {
        struct add_link_ctx *ctx = g_malloc0(sizeof(*ctx));
        GtkTextIter link_end = *location;
        (void) gtk_text_iter_backward_char(&link_end);

        gtk_text_iter_forward_char(&start_location);
        gtk_text_iter_forward_char(&start_location);
        g_print("NAME: %s\n", name);

        ctx->start_mark = gtk_text_buffer_create_mark(self, NULL,
                                                      &start_location, TRUE);
        ctx->stop_mark = gtk_text_buffer_create_mark(self, NULL, &link_end,
                                                     TRUE);
        ctx->page = page;
        g_idle_add_once(add_link_anchor, ctx);
      }
      g_debug("Stop link");
      g_free(name);
    }
    last = ' ';
    return;
  }
  last = text[0];
  g_print("Inserted txt: %s\n", text);
}

static GtkTextMark *
fix_last_anchor(EditorPage *page, GtkTextMark *start_mark)
{
  GtkTextChildAnchor *anchor;
  EditorPage *other;
  GtkTextBuffer *buffer;
  GtkTextIter start;
  GtkTextIter match_begin_start;
  GtkTextIter match_begin_end;
  GtkTextIter match_stop_start;
  GtkTextIter match_stop_end;
  gchar *name;
  GtkTextMark *return_mark = NULL;
  GtkWidget *button;

  buffer = page->content;

  if (start_mark != NULL) {
    gtk_text_buffer_get_iter_at_mark(buffer, &start, start_mark);
  } else {
    gtk_text_buffer_get_end_iter(buffer, &start);
  }

  if (!gtk_text_iter_backward_search(&start, "]]", GTK_TEXT_SEARCH_TEXT_ONLY,
                                     &match_stop_start, &match_stop_end, NULL)) {
    g_print("DID NOT FIND ]]\n");
    return NULL;
  }

  if (!gtk_text_iter_backward_search(&match_stop_start, "[[",
                                     GTK_TEXT_SEARCH_TEXT_ONLY,
                                     &match_begin_start, &match_begin_end,
                                     NULL)) {
    g_print("DID NOT FIND [[\n");
    return NULL;
  }

  gtk_text_iter_backward_char(&match_begin_start);
  return_mark = gtk_text_buffer_create_mark(buffer, NULL, &match_begin_start,
                                            TRUE);
  gtk_text_iter_forward_char(&match_begin_start);

  name = gtk_text_iter_get_text(&match_begin_end, &match_stop_start);

  g_print("FOUND NAME: %s\n", name);
  if (!validate_name_only(name)) {
    return_mark = gtk_text_buffer_create_mark(buffer, NULL, &match_begin_start,
                                              TRUE);
    goto out;
  }

  return_mark = gtk_text_buffer_create_mark(buffer, NULL, &match_begin_start,
                                            TRUE);

  /* Invalidates all the iterators above */
  gtk_text_buffer_delete(buffer, &match_begin_start, &match_stop_end);

  gtk_text_buffer_get_iter_at_mark(buffer, &start, return_mark);
  anchor = gtk_text_buffer_create_child_anchor(buffer, &start);

  other = g_hash_table_lookup(page->pages, name);

  if (!other) {
    other = editor_page_new(name, page->pages, &page->color, page->created_cb,
                            page->user_data);
  }

  g_object_set_data(G_OBJECT(anchor), "target", other);

  g_ptr_array_add(page->anchors, g_object_ref(anchor));

  // gtk_text_view_add_child_at_anchor(textarea, widget, anchor);
  /* EMIT new anchor */
  button = editor_page_in_content_button(other);
  g_object_set_data(G_OBJECT(button), "anchor", anchor);
  g_object_set_data(G_OBJECT(button), "target", page);

  g_signal_emit(page, editor_signals[EDITOR_PAGE_NEW_ANCHOR], 0, anchor, button);
  g_print("Returning a mark\n");
out:
  g_free(name);
  return return_mark;
}

static void
fix_anchors(EditorPage *page)
{
  GtkTextMark *mark = NULL;

  mark = fix_last_anchor(page, mark);
  while (mark != NULL) {
    mark = fix_last_anchor(page, mark);
  }
}

static void
fix_tags(EditorPage *page)
{
  GtkTextIter start;
  GtkTextIter match_start;
  GtkTextIter match_end;
  GtkTextIter bold_end_iter;
  GtkTextMark *mark = NULL;
  GtkTextMark *bold_end = NULL;
  gboolean bold;

  gtk_text_buffer_get_end_iter(page->content, &start);

  while (true) {
    if (!gtk_text_iter_backward_search(&start, "**", GTK_TEXT_SEARCH_TEXT_ONLY,
                                       &match_start, &match_end, NULL)) {
      return;
    }

    mark = gtk_text_buffer_create_mark(page->content, NULL, &match_start, TRUE);

    if (!bold) {
      bold_end = gtk_text_buffer_create_mark(page->content, NULL, &match_start,
                                             TRUE);
      bold = TRUE;
    } else {
      gtk_text_buffer_get_iter_at_mark(page->content, &bold_end_iter, bold_end);
      gtk_text_buffer_apply_tag_by_name(page->content, "bold", &match_end,
                                        &bold_end_iter);
      bold = FALSE;
    }

    /* Invalidates all the iterators above */
    gtk_text_buffer_delete(page->content, &match_start, &match_end);

    gtk_text_buffer_get_iter_at_mark(page->content, &start, mark);
  }
}

static void
foreach_button_name(gpointer data, gpointer user_data)
{
  GtkButton *button = GTK_BUTTON(data);
  GtkLabel *label;
  gchar *name = (gchar *) user_data;

  label = GTK_LABEL(gtk_button_get_child(button));
  gtk_label_set_text(label, name);
}

static void
update_name(EditorPage *self)
{
  GtkLabel *label;

  if (!self->page_button) {
    return;
  }

  label = GTK_LABEL(gtk_button_get_child(GTK_BUTTON(self->page_button)));
  gtk_label_set_text(label, self->heading);

  g_ptr_array_foreach(self->buttons, foreach_button_name, self->heading);
}

static void
editor_page_finalize(GObject *obj)
{
  EditorPage *self = EDITOR_PAGE(obj);

  g_assert(self);

  /*free stuff */

  g_free(self->heading);

  g_clear_object(&self->content);

  g_clear_object(&self->page_button);

  /* Always chain up to the parent finalize function to complete object
   * destruction. */
  G_OBJECT_CLASS(editor_page_parent_class)->finalize(obj);
}

static void
get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
  EditorPage *self = EDITOR_PAGE(object);

  switch ((EditorPageProperty) property_id) {
  case PROP_HEADING:
    g_value_set_string(value, self->heading);
    break;

  case PROP_CONTENT:
    g_value_set_object(value, self->content);
    break;

  case PROP_PAGE_BUTTON:
    g_value_set_object(value, self->page_button);
    break;

  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void
set_property(GObject *object,
             guint property_id,
             const GValue *value,
             GParamSpec *pspec)
{
  EditorPage *self = EDITOR_PAGE(object);

  switch ((EditorPageProperty) property_id) {
  case PROP_HEADING:
    g_free(self->heading);
    self->heading = g_value_dup_string(value);
    update_name(self);
    break;
  case PROP_CONTENT:
    g_clear_object(&self->content);
    self->content = g_value_get_object(value);
    break;
  case PROP_PAGE_BUTTON:
    g_clear_object(&self->page_button);
    self->page_button = g_value_get_object(value);
    break;
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void
editor_page_class_init(EditorPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = editor_page_finalize;
  object_class->set_property = set_property;
  object_class->get_property = get_property;

  obj_properties[PROP_HEADING] = g_param_spec_string("heading", "Heading",
                                                     "Placeholder "
                                                     "description.",
                                                     NULL, G_PARAM_READWRITE);

  obj_properties[PROP_CONTENT] = g_param_spec_object("content", "Content",
                                                     "Placeholder "
                                                     "description.",
                                                     GTK_TYPE_TEXT_BUFFER, /* default
                                                                            */
                                                     G_PARAM_READWRITE);

  obj_properties[PROP_PAGE_BUTTON] = g_param_spec_object("page_button",
                                                         "Page_button",
                                                         "Placeholder "
                                                         "description.",
                                                         GTK_TYPE_BUTTON, /* default
                                                                           */
                                                         G_PARAM_READWRITE);

  g_object_class_install_properties(object_class, N_PROPERTIES, obj_properties);

  editor_signals[EDITOR_PAGE_SWITCH] = g_signal_newv("switch-page",
                                                     G_TYPE_FROM_CLASS(klass),
                                                     G_SIGNAL_RUN_LAST |
                                                       G_SIGNAL_NO_RECURSE |
                                                       G_SIGNAL_NO_HOOKS,
                                                     NULL, NULL, NULL, NULL,
                                                     G_TYPE_NONE, 0, NULL);

  GType params[] = { G_TYPE_OBJECT, G_TYPE_OBJECT };
  editor_signals[EDITOR_PAGE_NEW_ANCHOR] = g_signal_newv("new-anchor",
                                                         G_TYPE_FROM_CLASS(klass),
                                                         G_SIGNAL_RUN_LAST |
                                                           G_SIGNAL_NO_RECURSE |
                                                           G_SIGNAL_NO_HOOKS,
                                                         NULL, NULL, NULL, NULL,
                                                         G_TYPE_NONE, 2, params);
}

static void
set_color(EditorPage *self, GdkRGBA *color)
{
  if (color != NULL) {
    self->color.red = color->red;
    self->color.green = color->green;
    self->color.blue = color->blue;
    self->color.alpha = color->alpha;
  }
}

static void
editor_page_init(EditorPage *self)
{
  /* initialize all public and private members to reasonable default values.
   * They are all automatically initialized to 0 to begin with. */

  if (self->heading == NULL) {
    g_print("Before the args\n");
  } else {
    g_print("After the args\n");
  }
  /* Not sharing tags (for now at least) */
  self->content = gtk_text_buffer_new(NULL);
  self->anchors = g_ptr_array_new();
  self->buttons = g_ptr_array_new();
  self->color.red = .7;
  self->color.green = .7;
  self->color.blue = 1.0;
  self->color.alpha = 1.0;

  self->bold = gtk_text_buffer_create_tag(self->content, "bold", "weight", 800,
                                          NULL);
}

static void
change_page(G_GNUC_UNUSED GObject *button, EditorPage *self)
{
  g_print("Emit change page\n");
  g_signal_emit(self, editor_signals[EDITOR_PAGE_SWITCH], 0, self);
  // EMIT change page
}

EditorPage *
editor_page_new(const gchar *heading,
                GHashTable *pages,
                GdkRGBA *color,
                GCallback created_cb,
                gpointer user_data)
{
  static guint css_num = 0;
  EditorPage *self;

  self = g_object_new(EDITOR_TYPE_PAGE, "heading", heading, "page_button",
                      gtk_button_new_with_label(heading), NULL);

  css_num++;
  self->css_name = g_strdup_printf("page%u", css_num);
  self->pages = pages;
  g_hash_table_insert(pages, g_strdup(heading), self);

  set_color(self, color);

  gtk_widget_add_css_class(self->page_button, self->css_name);

  g_signal_connect(self->page_button, "clicked", G_CALLBACK(change_page), self);
  g_signal_connect(self->content, "insert-text", G_CALLBACK(insert_text), self);

  g_object_set_data(G_OBJECT(self->page_button), "page", self);

  self->created_cb = created_cb;
  self->user_data = user_data;
  ((create_cb) *self->created_cb)(self, self->user_data);

  return self;
}

GtkWidget *
editor_page_in_content_button(EditorPage *self)
{
  GtkWidget *button;

  button = gtk_button_new_with_label(self->heading);
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  g_signal_connect(button, "clicked", G_CALLBACK(change_page), self);
  gtk_widget_add_css_class(button, "in-text-button");
  gtk_widget_add_css_class(button, self->css_name);

  g_ptr_array_add(self->buttons, g_object_ref(button));
  return button;
}

void
editor_page_selected_to_heading(EditorPage *self)
{
  GtkTextIter start;
  GtkTextIter end;
  if (!gtk_text_buffer_get_selection_bounds(self->content, &start, &end)) {
    return;
  }

  gtk_text_buffer_apply_tag_by_name(self->content, "bold", &start, &end);
}

GString *
editor_page_to_md(EditorPage *self)
{
  GString *res = g_string_new("");
  GtkTextIter iter;
  gunichar c;
  GtkTextChildAnchor *anchor;

  g_string_append_printf(res, "#%s\n", self->heading);

  gtk_text_buffer_get_start_iter(self->content, &iter);

  while ((c = gtk_text_iter_get_char(&iter)) > 0) {
    if (gtk_text_iter_starts_tag(&iter, self->bold) ||
        gtk_text_iter_ends_tag(&iter, self->bold)) {
      g_string_append(res, "**");
    }

    if (c == 0xFFFC) {
      /** unknown char - deal with anchors */
      anchor = gtk_text_iter_get_child_anchor(&iter);
      if (anchor != NULL) {
        EditorPage *target = g_object_get_data(G_OBJECT(anchor), "target");
        g_string_append_printf(res, "[[%s]]", target->heading);
      }
    } else {
      g_string_append_unichar(res, c);
    }

    gtk_text_iter_forward_char(&iter);
  }

  return res;
}

EditorPage *
editor_page_load(GHashTable *pages,
                 gchar *filename,
                 GdkRGBA *color,
                 GCallback created_cb,
                 gpointer user_data)
{
  GError *lerr = NULL;
  gchar *content = NULL;
  gsize size;
  gchar *name;
  EditorPage *page;
  gchar *text;

  if (!g_file_get_contents(filename, &content, &size, &lerr)) {
    g_warning("Could not open file: %s", lerr->message);
    g_clear_error(&lerr);
    return NULL;
  }

  if (!g_str_has_prefix(content, "#")) {
    return NULL;
  }

  text = g_strstr_len(content, size, "\n");
  if (text == NULL) {
    NULL;
  }

  name = g_strndup(content + 1, text - content - 1);

  g_print("Name: %s\n", name);

  page = g_hash_table_lookup(pages, name);

  if (page == NULL) {
    page = editor_page_new(name, pages, color, created_cb, user_data);
  } else {
    set_color(page, color);
  }

  gtk_text_buffer_set_text(page->content, text + 1, -1);

  g_free(content);

  return page;
}

void
editor_page_fix_content(EditorPage *page)
{
  g_print("Fixing anchors \n");
  fix_anchors(page);

  fix_tags(page);

  g_print("Free content\n");
}
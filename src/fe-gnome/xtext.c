/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * =========================================================================
 *
 * xtext, the text widget used by X-Chat.
 * By Peter Zelezny <zed@xchat.org>.
 *
 */

#define GDK_MULTIHEAD_SAFE

#define MARGIN 2						/* dont touch. */
#define REFRESH_TIMEOUT 20
#define WORDWRAP_LIMIT 24

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "../../config.h"

#include "xtext.h"
#include "xg-marshal.h"

#define charlen(str) g_utf8_skip[*(guchar *)(str)]

/* is delimiter */
#define is_del(c) \
	(c == ' ' || c == '\n' || c == ')' || c == '(' || \
	 c == '>' || c == '<' || c == ATTR_RESET || c == ATTR_BOLD || c == 0)

static GtkWidgetClass *parent_class = NULL;

struct textentry
{
	struct textentry *next;
	struct textentry *prev;
	unsigned char *str;
	time_t stamp;
	gint16 str_width;
	gint16 str_len;
	gint16 mark_start;
	gint16 mark_end;
	gint16 indent;
	gint16 left_len;
	gint16 lines_taken;
#define RECORD_WRAPS 4
	guint16 wrap_offset[RECORD_WRAPS];
	unsigned int mb:1;	/* is multibyte? */
};

enum
{
	WORD_CLICK,
	WORD_ENTER,
	WORD_LEAVE,
	LAST_SIGNAL
};

/* values for selection info */
enum
{
	TARGET_UTF8_STRING,
	TARGET_STRING,
	TARGET_TEXT,
	TARGET_COMPOUND_TEXT
};

static guint xtext_signals[LAST_SIGNAL];

char *nocasestrstr (const char *text, const char *tofind);	/* util.c */
int xtext_get_stamp_str (time_t, char **);

static void gtk_xtext_render_page (GtkXText * xtext);
static void gtk_xtext_calc_lines (xtext_buffer *buf, int);
static char *gtk_xtext_selection_get_text (GtkXText *xtext, int *len_ret);
static textentry *gtk_xtext_nth (GtkXText *xtext, int line, int *subline);
static void gtk_xtext_adjustment_changed (GtkAdjustment * adj,
														GtkXText * xtext);
static int gtk_xtext_render_ents (GtkXText * xtext, textentry *, textentry *);
static void gtk_xtext_recalc_widths (xtext_buffer *buf, int);
static void gtk_xtext_fix_indent (xtext_buffer *buf);
static int gtk_xtext_find_subline (GtkXText *xtext, textentry *ent, int line);
static unsigned char *
gtk_xtext_strip_color (unsigned char *text, int len, unsigned char *outbuf,
							  int *newlen, int *mb_ret, int strip_hidden);


/* gives width of a 8bit string - with no mIRC codes in it */

static int
gtk_xtext_text_width_8bit (GtkXText *xtext, unsigned char *str, int len)
{
	int width = 0;

	while (len)
	{
		width += xtext->fontwidth[*str];
		str++;
		len--;
	}

	return width;
}

/* Inlined for speed sake */
static inline GdkColor 
xtext_palette_lookup (GtkXText *xtext, int index)
{
    return xtext->palette[index];
}

static inline void
xtext_set_source_color (cairo_t *cr, GdkColor *color, gdouble alpha)
{
    cairo_set_source_rgba (cr, color->red / 65535.0, color->green / 65535.0,
        color->blue / 65535.0, alpha);
}

static inline void
xtext_draw_rectangle (GtkXText *xtext, cairo_t *cr, GdkColor *color, int x, int y, int width, int height)
{
	cairo_save (cr);

    xtext_set_source_color (cr, color, xtext->alpha);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle (cr, (double)x, (double)y, (double)width, (double)height);
	cairo_fill (cr);

	cairo_restore (cr);
}

static inline void
xtext_draw_line (GtkXText *xtext, cairo_t *cr, GdkColor *color, int x1, int y1, int x2, int y2)
{
    cairo_save (cr);

    /* Disable antialiasing for crispy 1-pixel lines */
    cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);
    gdk_cairo_set_source_color (cr, color);
    cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_line_width (cr, 1.0);
    cairo_move_to (cr, x1, y1);
    cairo_line_to (cr, x2, y2);
    cairo_stroke (cr);

    cairo_restore (cr);
}

static inline void
xtext_draw_bg (GtkXText *xtext, int x, int y, int width, int height)
{
	cairo_t *cr;
	cr = gdk_cairo_create (gtk_widget_get_window (&xtext->widget));
    xtext_draw_rectangle(xtext, cr, &xtext->palette[XTEXT_BG], x, y, width, height);
	cairo_destroy (cr);
}

void
gtk_xtext_set_alpha (GtkXText *xtext, double alpha)
{
	xtext->alpha = alpha;
}

/* ======================================= */
/* ============ PANGO BACKEND ============ */
/* ======================================= */

static void
backend_font_close (GtkXText *xtext)
{
	pango_font_description_free (xtext->font->font);
}

static void
backend_init (GtkXText *xtext)
{
	if (xtext->layout == NULL)
	{
		xtext->layout = gtk_widget_create_pango_layout (GTK_WIDGET (xtext), 0); 
		if (xtext->font)
			pango_layout_set_font_description (xtext->layout, xtext->font->font);
	}
}

static void
backend_deinit (GtkXText *xtext)
{
	if (xtext->layout)
	{
		g_object_unref (xtext->layout);
		xtext->layout = NULL;
	}
}

static PangoFontDescription *
backend_font_open_real (char *name)
{
	PangoFontDescription *font;

	font = pango_font_description_from_string (name);
	if (font && pango_font_description_get_size (font) == 0)
	{
		pango_font_description_free (font);
		font = pango_font_description_from_string ("sans 11");
	}
	if (!font)
		font = pango_font_description_from_string ("sans 11");

	return font;
}

static void
backend_font_open (GtkXText *xtext, char *name)
{
	PangoLanguage *lang;
	PangoContext *context;
	PangoFontMetrics *metrics;

	xtext->font = &xtext->pango_font;
	xtext->font->font = backend_font_open_real (name);
	if (!xtext->font->font)
	{
		xtext->font = NULL;
		return;
	}

	backend_init (xtext);
	pango_layout_set_font_description (xtext->layout, xtext->font->font);

	/* vte does it this way */
	context = gtk_widget_get_pango_context (GTK_WIDGET (xtext));
	lang = pango_context_get_language (context);
	metrics = pango_context_get_metrics (context, xtext->font->font, lang);
	xtext->font->ascent = pango_font_metrics_get_ascent (metrics) / PANGO_SCALE;
	xtext->font->descent = pango_font_metrics_get_descent (metrics) / PANGO_SCALE;
	pango_font_metrics_unref (metrics);
}

static int
backend_get_text_width (GtkXText *xtext, guchar *str, int len, int is_mb)
{
	int width;

	if (!is_mb)
		return gtk_xtext_text_width_8bit (xtext, str, len);

	if (*str == 0)
		return 0;

	pango_layout_set_text (xtext->layout, (char *) str, len);
	pango_layout_get_pixel_size (xtext->layout, &width, NULL);

	return width;
}

inline static int
backend_get_char_width (GtkXText *xtext, unsigned char *str, int *mbl_ret)
{
	int width;

	if (*str < 128)
	{
		*mbl_ret = 1;
		return xtext->fontwidth[*str];
	}

	*mbl_ret = charlen (str);
	pango_layout_set_text (xtext->layout, (char *) str, *mbl_ret);
	pango_layout_get_pixel_size (xtext->layout, &width, NULL);

	return width;
}

/* simplified version of gdk_draw_layout_line_with_colors() */

static void 
xtext_draw_layout_line (cairo_t *cr, int x, int y, PangoLayoutLine  *line)
{
	cairo_save (cr);
	cairo_move_to (cr, x, y);
	pango_cairo_show_layout_line (cr, line);
	cairo_restore (cr);
}

static void
backend_draw_text (GtkXText *xtext, int dofill, int x, int y,
						 char *str, int len, int str_width, int is_mb)
{
	PangoLayoutLine *line;
	PangoAttrList *attr_list;
	PangoAttribute *attr;
	attr_list = pango_attr_list_new ();
	
	cairo_t *cr;

	cr = gdk_cairo_create (gtk_widget_get_window (&xtext->widget));

	if (xtext->italics)
	{
		attr = pango_attr_style_new (PANGO_STYLE_ITALIC);
		attr->start_index = PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
		attr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;

		pango_attr_list_insert (attr_list, attr);
	}
	if (xtext->bold)
	{
		attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
		attr->start_index = PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
		attr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;

		pango_attr_list_insert (attr_list, attr);
	}
	
	pango_layout_set_attributes (xtext->layout, attr_list);
	pango_layout_set_text (xtext->layout, str, len);

	if (dofill)
	{
		xtext_draw_rectangle (xtext, cr, &xtext->text_bg, x, y - xtext->font->ascent, 
				str_width, xtext->fontsize);
	}

    gdk_cairo_set_source_color (cr, &xtext->text_fg);
	line = pango_layout_get_lines (xtext->layout)->data;

	xtext_draw_layout_line (cr, x, y, line);

	pango_attr_list_unref (attr_list);

	cairo_destroy (cr);
}

static void
gtk_xtext_init (GtkXText * xtext)
{
	xtext->io_tag = 0;
	xtext->add_io_tag = 0;
	xtext->scroll_tag = 0;
	xtext->max_lines = 0;
	xtext->col_back = XTEXT_BG;
	xtext->col_fore = XTEXT_FG;
	xtext->nc = 0;
	xtext->pixel_offset = 0;
	xtext->bold = FALSE;
	xtext->underline = FALSE;
	xtext->italics = FALSE;
	xtext->hidden = FALSE;
	xtext->font = NULL;
	xtext->layout = NULL;
	xtext->jump_out_offset = 0;
	xtext->jump_in_offset = 0;
	xtext->clip_x = 0;
	xtext->clip_x2 = 1000000;
	xtext->clip_y = 0;
	xtext->clip_y2 = 1000000;
	xtext->error_function = NULL;
	xtext->urlcheck_function = NULL;
	xtext->color_paste = FALSE;
	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
	xtext->render_hilights_only = FALSE;
	xtext->un_hilight = FALSE;
	xtext->recycle = FALSE;
	xtext->dont_render = FALSE;
	xtext->dont_render2 = FALSE;

	xtext->adj = (GtkAdjustment *) gtk_adjustment_new (0, 0, 1, 1, 1, 1);
	g_object_ref (G_OBJECT (xtext->adj));
	g_object_ref_sink (G_OBJECT (xtext->adj));
	g_object_unref (G_OBJECT (xtext->adj));

	xtext->vc_signal_tag = g_signal_connect (G_OBJECT (xtext->adj),
				"value_changed", G_CALLBACK (gtk_xtext_adjustment_changed), xtext);
	{
		static const GtkTargetEntry targets[] = {
			{ "UTF8_STRING", 0, TARGET_UTF8_STRING },
			{ "STRING", 0, TARGET_STRING },
			{ "TEXT",   0, TARGET_TEXT }, 
			{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
		};
		static const gint n_targets = sizeof (targets) / sizeof (targets[0]);

		gtk_selection_add_targets (GTK_WIDGET (xtext), GDK_SELECTION_PRIMARY,
											targets, n_targets);
	}
}

static void
gtk_xtext_adjustment_set (xtext_buffer *buf, int fire_signal)
{
	GtkAdjustment *adj = buf->xtext->adj;
	GtkAllocation allocation;
	int val;

	if (buf->xtext->buffer == buf)
	{
		gtk_adjustment_set_lower (adj, 0);
		gtk_adjustment_set_upper (adj, buf->num_lines? buf->num_lines: 1);

		gtk_widget_get_allocation (GTK_WIDGET(buf->xtext), &allocation);

		gtk_adjustment_set_page_size (adj,
			(allocation.height - buf->xtext->font->descent) / buf->xtext->fontsize);
		gtk_adjustment_set_page_increment (adj, gtk_adjustment_get_page_size (adj));

		val = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj);

		if (gtk_adjustment_get_value (adj) > val)
			gtk_adjustment_set_value (adj, val);

		if (gtk_adjustment_get_value (adj) < 0)
			gtk_adjustment_set_value (adj, 0);

		if (fire_signal)
			gtk_adjustment_changed (adj);
	}
}

static gint
gtk_xtext_adjustment_timeout (GtkXText * xtext)
{
	gtk_xtext_render_page (xtext);
	xtext->io_tag = 0;
	return 0;
}

static void
gtk_xtext_adjustment_changed (GtkAdjustment * adj, GtkXText * xtext)
{
	if (xtext->buffer->old_value != gtk_adjustment_get_value (xtext->adj))
	{
		if (gtk_adjustment_get_value (xtext->adj) >= gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj))
			xtext->buffer->scrollbar_down = TRUE;
		else
			xtext->buffer->scrollbar_down = FALSE;

		if (gtk_adjustment_get_value (xtext->adj) + 1 == xtext->buffer->old_value ||
			gtk_adjustment_get_value (xtext->adj) - 1 == xtext->buffer->old_value)	/* clicked an arrow? */
		{
			if (xtext->io_tag)
			{
				g_source_remove (xtext->io_tag);
				xtext->io_tag = 0;
			}
			gtk_xtext_render_page (xtext);
		} else
		{
			if (!xtext->io_tag)
				xtext->io_tag = g_timeout_add (REFRESH_TIMEOUT,
															(GSourceFunc)
															gtk_xtext_adjustment_timeout,
															xtext);
		}
	}
	xtext->buffer->old_value = gtk_adjustment_get_value (adj);
}

GtkWidget *
gtk_xtext_new (GdkColor palette[], int separator)
{
	GtkXText *xtext;

	xtext = g_object_new (gtk_xtext_get_type (), NULL);
	xtext->separator = separator;
	xtext->wordwrap = TRUE;
	xtext->buffer = gtk_xtext_buffer_new (xtext);
	xtext->orig_buffer = xtext->buffer;
	xtext->alpha = 1.0;

	gtk_xtext_set_palette (xtext, palette);

	return GTK_WIDGET (xtext);
}

static void
gtk_xtext_destroy (GObject * object)
{
	GtkXText *xtext = GTK_XTEXT (object);

	if (xtext->add_io_tag)
	{
		g_source_remove (xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->scroll_tag)
	{
		g_source_remove (xtext->scroll_tag);
		xtext->scroll_tag = 0;
	}

	if (xtext->io_tag)
	{
		g_source_remove (xtext->io_tag);
		xtext->io_tag = 0;
	}

	if (xtext->font)
	{
		backend_font_close (xtext);
		xtext->font = NULL;
	}

	if (xtext->adj)
	{
		g_signal_handlers_disconnect_matched (G_OBJECT (xtext->adj),
					G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, xtext);
	/*	gtk_signal_disconnect_by_data (GTK_OBJECT (xtext->adj), xtext);*/
		g_object_unref (G_OBJECT (xtext->adj));
		xtext->adj = NULL;
	}

	if (xtext->hand_cursor)
	{
		gdk_cursor_unref (xtext->hand_cursor);
		xtext->hand_cursor = NULL;
	}

	if (xtext->resize_cursor)
	{
		gdk_cursor_unref (xtext->resize_cursor);
		xtext->resize_cursor = NULL;
	}

	if (xtext->orig_buffer)
	{
		gtk_xtext_buffer_free (xtext->orig_buffer);
		xtext->orig_buffer = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gtk_xtext_unrealize (GtkWidget * widget)
{
	backend_deinit (GTK_XTEXT (widget));

	/* if there are still events in the queue, this'll avoid segfault */
	gdk_window_set_user_data (gtk_widget_get_window (widget), NULL);

	if (parent_class->unrealize)
		(* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void
gtk_xtext_realize (GtkWidget * widget)
{
	GtkXText *xtext;
	GdkWindowAttr attributes;
	GdkWindow *w;
	GtkAllocation allocation;

	gtk_widget_set_realized (widget, TRUE);
	xtext = GTK_XTEXT (widget);

	gtk_widget_get_allocation (widget, &allocation);

	attributes.x = allocation.x;
	attributes.y = allocation.y;
	attributes.width = allocation.width;
	attributes.height = allocation.height;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events (widget) |
		GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK;

	attributes.visual = gtk_widget_get_visual (widget);

	w = gdk_window_new (gtk_widget_get_parent_window (widget), 
			&attributes, GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
	gtk_widget_set_window (widget, w);

	gdk_window_set_user_data (w, widget);

	/* for the separator bar (light) */
	xtext->light_gc.red   = 0xffff; 
    xtext->light_gc.green = 0xffff; 
    xtext->light_gc.blue  = 0xffff;

	/* for the separator bar (dark) */
	xtext->dark_gc.red    = 0x1111; 
    xtext->dark_gc.green  = 0x1111; 
    xtext->dark_gc.blue   = 0x1111;

	/* for the separator bar (thinline) */
	xtext->thin_gc.red    = 0x8e38; 
    xtext->thin_gc.green  = 0x8e38; 
    xtext->thin_gc.blue   = 0x9f38;

	/* for the marker bar (marker) */
	xtext->marker_gc = xtext_palette_lookup (xtext, XTEXT_MARKER);

    xtext->text_fg = xtext_palette_lookup (xtext, XTEXT_FG);
    xtext->text_bg = xtext_palette_lookup (xtext, XTEXT_BG);

	xtext->hand_cursor = gdk_cursor_new_for_display (gdk_window_get_display (w), GDK_HAND1);
	xtext->resize_cursor = gdk_cursor_new_for_display (gdk_window_get_display (w), GDK_LEFT_SIDE);

	gtk_widget_set_style (widget, gtk_style_attach (gtk_widget_get_style (widget), w));

	backend_init (xtext);
}

static void
gtk_xtext_get_preferred_width (GtkWidget * widget, gint *minimal_width, gint *natural_width)
{
	*minimal_width = *natural_width = 200;
}

static void
gtk_xtext_get_preferred_height (GtkWidget * widget, gint *minimal_height, gint *natural_height)
{
	*minimal_height = *natural_height = 90;
}

static void
gtk_xtext_size_allocate (GtkWidget * widget, GtkAllocation * allocation)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	int height_only = FALSE;

	if (allocation->width == xtext->buffer->window_width)
		height_only = TRUE;

	gtk_widget_set_allocation (widget, allocation);
	if (gtk_widget_get_realized (widget))
	{
		xtext->buffer->window_width = allocation->width;
		xtext->buffer->window_height = allocation->height;

		gdk_window_move_resize (gtk_widget_get_window (widget), allocation->x, allocation->y,
			allocation->width, allocation->height);

		if (!height_only)
			gtk_xtext_calc_lines (xtext->buffer, FALSE);
		else
		{
			xtext->buffer->pagetop_ent = NULL;
			gtk_xtext_adjustment_set (xtext->buffer, FALSE);
		}
		if (xtext->buffer->scrollbar_down)
			gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj) -
				gtk_adjustment_get_page_size (xtext->adj));
	}
}

void
gtk_xtext_selection_clear_full (xtext_buffer *buf)
{
	textentry *ent = buf->text_first;
	while (ent)
	{
		ent->mark_start = -1;
		ent->mark_end = -1;
		ent = ent->next;
	}
}

static int
gtk_xtext_selection_clear (xtext_buffer *buf)
{
	textentry *ent;
	int ret = 0;

	ent = buf->last_ent_start;
	while (ent)
	{
		if (ent->mark_start != -1)
			ret = 1;
		ent->mark_start = -1;
		ent->mark_end = -1;
		if (ent == buf->last_ent_end)
			break;
		ent = ent->next;
	}

	return ret;
}

static int
find_x (GtkXText *xtext, textentry *ent, unsigned char *text, int x, int indent)
{
	int xx = indent;
	int i = 0;
	int rcol = 0, bgcol = 0;
	int hidden = FALSE;
	unsigned char *orig = text;
	int mbl;
	int char_width;

	while (*text)
	{
		mbl = 1;
		if (rcol > 0 && (isdigit (*text) || (*text == ',' && isdigit (text[1]) && !bgcol)))
		{
			if (text[1] != ',') rcol--;
			if (*text == ',')
			{
				rcol = 2;
				bgcol = 1;
			}
			text++;
		} else
		{
			rcol = bgcol = 0;
			switch (*text)
			{
			case ATTR_COLOR:
				rcol = 2;
			case ATTR_BEEP:
			case ATTR_RESET:
			case ATTR_REVERSE:
			case ATTR_BOLD:
			case ATTR_UNDERLINE:
			case ATTR_ITALICS:
				text++;
				break;
			case ATTR_HIDDEN:
				if (xtext->ignore_hidden)
					goto def;
				hidden = !hidden;
				text++;
				break;
			default:
			def:
				char_width = backend_get_char_width (xtext, text, &mbl);
				if (!hidden) xx += char_width;
				text += mbl;
				if (xx >= x)
					return i + (orig - ent->str);
			}
		}

		i += mbl;
		if (text - orig >= ent->str_len)
			return ent->str_len;
	}

	return ent->str_len;
}

static int
gtk_xtext_find_x (GtkXText * xtext, int x, textentry * ent, int subline,
						int line, int *out_of_bounds)
{
	int indent;
	unsigned char *str;

	if (subline < 1)
		indent = ent->indent;
	else
		indent = xtext->buffer->indent;

	if (line > gtk_adjustment_get_page_size (xtext->adj) || line < 0)
		return 0;

	if (xtext->buffer->grid_dirty || line > 255)
	{
		str = ent->str + gtk_xtext_find_subline (xtext, ent, subline);
		if (str >= ent->str + ent->str_len)
			return 0;
	} else
	{
		if (xtext->buffer->grid_offset[line] > ent->str_len)
			return 0;
		str = ent->str + xtext->buffer->grid_offset[line];
	}

	if (x < indent)
	{
		*out_of_bounds = 1;
		return (str - ent->str);
	}

	*out_of_bounds = 0;

	return find_x (xtext, ent, str, x, indent);
}

static textentry *
gtk_xtext_find_char (GtkXText * xtext, int x, int y, int *off,
							int *out_of_bounds)
{
	textentry *ent;
	int line;
	int subline;

	line = (y + xtext->pixel_offset) / xtext->fontsize;
	ent = gtk_xtext_nth (xtext, line + gtk_adjustment_get_value (xtext->adj), &subline);
	if (!ent)
		return 0;

	if (off)
		*off = gtk_xtext_find_x (xtext, x, ent, subline, line, out_of_bounds);

	return ent;
}

static void
gtk_xtext_draw_sep (GtkXText * xtext, int y)
{
	int x, height;
	GdkColor *light, *dark;
	GtkAllocation allocation;
    cairo_t *cr;

	if (y == -1)
	{
		y = 0;
		gtk_widget_get_allocation (GTK_WIDGET (xtext), &allocation);
		height = allocation.height;
	} else
	{
		height = xtext->fontsize;
	}

	/* draw the separator line */
	if (xtext->separator && xtext->buffer->indent)
	{
        light = &xtext->light_gc;
		dark = &xtext->dark_gc;

		x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (x < 1)
			return;

		cr = gdk_cairo_create (gtk_widget_get_window (&xtext->widget));

		if (xtext->thinline)
		{
			if (xtext->moving_separator)
                xtext_draw_line (xtext, cr, light, x, y, x, y + height);
			else
                xtext_draw_line (xtext, cr, &xtext->thin_gc, x, y, x, y + height);
		} else
		{
			if (xtext->moving_separator)
			{
				xtext_draw_line (xtext, cr, light, x - 1, y, x - 1, y + height);
				xtext_draw_line (xtext, cr, dark, x, y, x, y + height);
			} else
			{
				xtext_draw_line (xtext, cr, dark, x - 1, y, x - 1, y + height);
				xtext_draw_line (xtext, cr, light, x, y, x, y + height);
			}
		}

        cairo_destroy (cr);
	}
}

static void
gtk_xtext_draw_marker (GtkXText * xtext, textentry * ent, int y)
{
	int x, width, render_y;
	GtkAllocation allocation;
    cairo_t *cr;

	if (!xtext->marker) return;

	if (xtext->buffer->marker_pos == ent)
	{
		render_y = y + xtext->font->descent;
	}
	else if (xtext->buffer->marker_pos == ent->next && ent->next != NULL)
	{
		render_y = y + xtext->font->descent + xtext->fontsize * ent->lines_taken;
	}
	else return;

	cr = gdk_cairo_create (gtk_widget_get_window (&xtext->widget));

	gtk_widget_get_allocation (GTK_WIDGET(xtext), &allocation);

	x = 0;
	width = allocation.width;

	xtext_draw_line (xtext, cr, &xtext->marker_gc, x, render_y, x + width, render_y);

	if (gtk_window_has_toplevel_focus (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (xtext)))))
	{
		xtext->buffer->marker_seen = TRUE;
	}

    cairo_destroy (cr);
}

static gboolean
gtk_xtext_draw (GtkWidget * widget, cairo_t *cr)
{
	gtk_xtext_render_page (GTK_XTEXT (widget));

	return FALSE;
}

/* render a selection that has extended or contracted upward */

static void
gtk_xtext_selection_up (GtkXText *xtext, textentry *start, textentry *end,
								int start_offset)
{
	/* render all the complete lines */
	if (start->next == end)
		gtk_xtext_render_ents (xtext, end, NULL);
	else
		gtk_xtext_render_ents (xtext, start->next, end);

	/* now the incomplete upper line */
	if (start == xtext->buffer->last_ent_start)
		xtext->jump_in_offset = xtext->buffer->last_offset_start;
	else
		xtext->jump_in_offset = start_offset;
	gtk_xtext_render_ents (xtext, start, NULL);
	xtext->jump_in_offset = 0;
}

/* render a selection that has extended or contracted downward */

static void
gtk_xtext_selection_down (GtkXText *xtext, textentry *start, textentry *end,
								  int end_offset)
{
	/* render all the complete lines */
	if (end->prev == start)
		gtk_xtext_render_ents (xtext, start, NULL);
	else
		gtk_xtext_render_ents (xtext, start, end->prev);

	/* now the incomplete bottom line */
	if (end == xtext->buffer->last_ent_end)
		xtext->jump_out_offset = xtext->buffer->last_offset_end;
	else
		xtext->jump_out_offset = end_offset;
	gtk_xtext_render_ents (xtext, end, NULL);
	xtext->jump_out_offset = 0;
}

static void
gtk_xtext_selection_render (GtkXText *xtext,
									 textentry *start_ent, int start_offset,
									 textentry *end_ent, int end_offset)
{
	textentry *ent;
	int start, end;

	xtext->skip_border_fills = TRUE;
	xtext->skip_stamp = TRUE;

	/* force an optimized render if there was no previous selection */
	if (xtext->buffer->last_ent_start == NULL && start_ent == end_ent)
	{
		xtext->buffer->last_offset_start = start_offset;
		xtext->buffer->last_offset_end = end_offset;
		goto lamejump;
	}

	/* mark changed within 1 ent only? */
	if (xtext->buffer->last_ent_start == start_ent &&
		 xtext->buffer->last_ent_end == end_ent)
	{
		/* when only 1 end of the selection is changed, we can really
			save on rendering */
		if (xtext->buffer->last_offset_start == start_offset ||
			 xtext->buffer->last_offset_end == end_offset)
		{
lamejump:
			ent = end_ent;
			/* figure out where to start and end the rendering */
			if (end_offset > xtext->buffer->last_offset_end)
			{
				end = end_offset;
				start = xtext->buffer->last_offset_end;
			} else if (end_offset < xtext->buffer->last_offset_end)
			{
				end = xtext->buffer->last_offset_end;
				start = end_offset;
			} else if (start_offset < xtext->buffer->last_offset_start)
			{
				end = xtext->buffer->last_offset_start;
				start = start_offset;
				ent = start_ent;
			} else if (start_offset > xtext->buffer->last_offset_start)
			{
				end = start_offset;
				start = xtext->buffer->last_offset_start;
				ent = start_ent;
			} else
			{	/* WORD selects end up here */
				end = end_offset;
				start = start_offset;
			}
		} else
		{
			/* LINE selects end up here */
			/* so which ent actually changed? */
			ent = start_ent;
			if (xtext->buffer->last_offset_start == start_offset)
				ent = end_ent;

			end = MAX (xtext->buffer->last_offset_end, end_offset);
			start = MIN (xtext->buffer->last_offset_start, start_offset);
		}

		xtext->jump_out_offset = end;
		xtext->jump_in_offset = start;
		gtk_xtext_render_ents (xtext, ent, NULL);
		xtext->jump_out_offset = 0;
		xtext->jump_in_offset = 0;
	}
	/* marking downward? */
	else if (xtext->buffer->last_ent_start == start_ent &&
				xtext->buffer->last_offset_start == start_offset)
	{
		/* find the range that covers both old and new selection */
		ent = start_ent;
		while (ent)
		{
			if (ent == xtext->buffer->last_ent_end)
			{
				gtk_xtext_selection_down (xtext, ent, end_ent, end_offset);
				/*gtk_xtext_render_ents (xtext, ent, end_ent);*/
				break;
			}
			if (ent == end_ent)
			{
				gtk_xtext_selection_down (xtext, ent, xtext->buffer->last_ent_end, end_offset);
				/*gtk_xtext_render_ents (xtext, ent, xtext->buffer->last_ent_end);*/
				break;
			}
			ent = ent->next;
		}
	}
	/* marking upward? */
	else if (xtext->buffer->last_ent_end == end_ent &&
				xtext->buffer->last_offset_end == end_offset)
	{
		ent = end_ent;
		while (ent)
		{
			if (ent == start_ent)
			{
				gtk_xtext_selection_up (xtext, xtext->buffer->last_ent_start, ent, start_offset);
				/*gtk_xtext_render_ents (xtext, xtext->buffer->last_ent_start, ent);*/
				break;
			}
			if (ent == xtext->buffer->last_ent_start)
			{
				gtk_xtext_selection_up (xtext, start_ent, ent, start_offset);
				/*gtk_xtext_render_ents (xtext, start_ent, ent);*/
				break;
			}
			ent = ent->prev;
		}
	}
	else	/* cross-over mark (stretched or shrunk at both ends) */
	{
		/* unrender the old mark */
		gtk_xtext_render_ents (xtext, xtext->buffer->last_ent_start, xtext->buffer->last_ent_end);
		/* now render the new mark, but skip overlaps */
		if (start_ent == xtext->buffer->last_ent_start)
		{
			/* if the new mark is a sub-set of the old, do nothing */
			if (start_ent != end_ent)
				gtk_xtext_render_ents (xtext, start_ent->next, end_ent);
		} else if (end_ent == xtext->buffer->last_ent_end)
		{
			/* if the new mark is a sub-set of the old, do nothing */
			if (start_ent != end_ent)
				gtk_xtext_render_ents (xtext, start_ent, end_ent->prev);
		} else
			gtk_xtext_render_ents (xtext, start_ent, end_ent);
	}

	xtext->buffer->last_ent_start = start_ent;
	xtext->buffer->last_ent_end = end_ent;
	xtext->buffer->last_offset_start = start_offset;
	xtext->buffer->last_offset_end = end_offset;

	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
}

static void
gtk_xtext_selection_draw (GtkXText * xtext, GdkEventMotion * event, gboolean render)
{
	textentry *ent;
	textentry *ent_end;
	textentry *ent_start;
	int offset_start;
	int offset_end;
	int low_x;
	int low_y;
	int high_x;
	int high_y;
	int tmp;

	if (xtext->select_start_y > xtext->select_end_y)
	{
		low_x = xtext->select_end_x;
		low_y = xtext->select_end_y;
		high_x = xtext->select_start_x;
		high_y = xtext->select_start_y;
	} else
	{
		low_x = xtext->select_start_x;
		low_y = xtext->select_start_y;
		high_x = xtext->select_end_x;
		high_y = xtext->select_end_y;
	}

	ent_start = gtk_xtext_find_char (xtext, low_x, low_y, &offset_start, &tmp);
	if (!ent_start)
	{
		if (gtk_adjustment_get_value (xtext->adj) != xtext->buffer->old_value)
			gtk_xtext_render_page (xtext);
		return;
	}

	ent_end = gtk_xtext_find_char (xtext, high_x, high_y, &offset_end, &tmp);
	if (!ent_end)
	{
		ent_end = xtext->buffer->text_last;
		if (!ent_end)
		{
			if (gtk_adjustment_get_value (xtext->adj) != xtext->buffer->old_value)
				gtk_xtext_render_page (xtext);
			return;
		}
		offset_end = ent_end->str_len;
	}

	/* marking less than a complete line? */
	/* make sure "start" is smaller than "end" (swap them if need be) */
	if (ent_start == ent_end && offset_start > offset_end)
	{
		tmp = offset_start;
		offset_start = offset_end;
		offset_end = tmp;
	}

	/* has the selection changed? Dont render unless necessary */
	if (xtext->buffer->last_ent_start == ent_start &&
		 xtext->buffer->last_ent_end == ent_end &&
		 xtext->buffer->last_offset_start == offset_start &&
		 xtext->buffer->last_offset_end == offset_end)
		return;

	/* set all the old mark_ fields to -1 */
	gtk_xtext_selection_clear (xtext->buffer);

	ent_start->mark_start = offset_start;
	ent_start->mark_end = offset_end;

	if (ent_start != ent_end)
	{
		ent_start->mark_end = ent_start->str_len;
		if (offset_end != 0)
		{
			ent_end->mark_start = 0;
			ent_end->mark_end = offset_end;
		}

		/* set all the mark_ fields of the ents within the selection */
		ent = ent_start->next;
		while (ent && ent != ent_end)
		{
			ent->mark_start = 0;
			ent->mark_end = ent->str_len;
			ent = ent->next;
		}
	}

	if (render)
		gtk_xtext_selection_render (xtext, ent_start, offset_start, ent_end, offset_end);
}

static gint
gtk_xtext_scrolldown_timeout (GtkXText * xtext)
{
	int p_y, win_height;

	gdk_window_get_pointer (gtk_widget_get_window (GTK_WIDGET(xtext)), 0, &p_y, 0);
	win_height = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(xtext)));

	if (p_y > win_height && gtk_adjustment_get_value (xtext->adj) < (gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj)))
	{
		gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_value (xtext->adj) + 1);
		gtk_adjustment_changed (xtext->adj);
		gtk_xtext_render_page (xtext);
		return 1;
	}

	xtext->scroll_tag = 0;
	return 0;
}

static gint
gtk_xtext_scrollup_timeout (GtkXText * xtext)
{
	int p_y;

	gdk_window_get_pointer (gtk_widget_get_window (GTK_WIDGET(xtext)), 0, &p_y, 0);

	if (p_y < 0 && gtk_adjustment_get_value (xtext->adj) > 0.0)
	{
		gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_value (xtext->adj) - 1);
		gtk_adjustment_changed (xtext->adj);
		gtk_xtext_render_page (xtext);
		return 1;
	}

	xtext->scroll_tag = 0;
	return 0;
}

static void
gtk_xtext_selection_update (GtkXText * xtext, GdkEventMotion * event, int p_y, gboolean render)
{
	int win_height;
	int moved;

	win_height = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(xtext)));

	/* selecting past top of window, scroll up! */
	if (p_y < 0 && gtk_adjustment_get_value (xtext->adj) >= 0)
	{
		if (!xtext->scroll_tag)
			xtext->scroll_tag = g_timeout_add (100,
															(GSourceFunc)
															gtk_xtext_scrollup_timeout,
															xtext);
		return;
	}

	/* selecting past bottom of window, scroll down! */
	if (p_y > win_height &&
		 gtk_adjustment_get_value (xtext->adj) < (gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj)))
	{
		if (!xtext->scroll_tag)
			xtext->scroll_tag = g_timeout_add (100,
															(GSourceFunc)
															gtk_xtext_scrolldown_timeout,
															xtext);
		return;
	}

	moved = gtk_adjustment_get_value (xtext->adj) - xtext->select_start_adj;
	xtext->select_start_y -= (moved * xtext->fontsize);
	xtext->select_start_adj = gtk_adjustment_get_value (xtext->adj);
	gtk_xtext_selection_draw (xtext, event, render);
}

static char *
gtk_xtext_get_word (GtkXText * xtext, int x, int y, textentry ** ret_ent,
						  int *ret_off, int *ret_len)
{
	textentry *ent;
	int offset;
	unsigned char *str;
	unsigned char *word;
	int len;
	int out_of_bounds = 0;

	ent = gtk_xtext_find_char (xtext, x, y, &offset, &out_of_bounds);
	if (!ent)
		return 0;

	if (out_of_bounds)
		return 0;

	if (offset == ent->str_len)
		return 0;

	if (offset < 1)
		return 0;

	/*offset--;*/	/* FIXME: not all chars are 1 byte */

	str = ent->str + offset;

	while (!is_del (*str) && str != ent->str)
		str--;
	word = str + 1;

	len = 0;
	str = word;
	while (!is_del (*str) && len != ent->str_len)
	{
		str++;
		len++;
	}

	if (len > 0 && word[len-1]=='.')
	{
		len--;
		str--;
	}

	if (ret_ent)
		*ret_ent = ent;
	if (ret_off)
		*ret_off = word - ent->str;
	if (ret_len)
		*ret_len = str - word;

	return (char *) gtk_xtext_strip_color (word, len, xtext->scratch_buffer, NULL, NULL, FALSE);
}

static void
gtk_xtext_unrender_hilight (GtkXText *xtext)
{
	xtext->render_hilights_only = TRUE;
	xtext->skip_border_fills = TRUE;
	xtext->skip_stamp = TRUE;
	xtext->un_hilight = TRUE;

	gtk_xtext_render_ents (xtext, xtext->hilight_ent, NULL);

	xtext->render_hilights_only = FALSE;
	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;
	xtext->un_hilight = FALSE;
}

static gboolean
gtk_xtext_leave_notify (GtkWidget * widget, GdkEventCrossing * event)
{
	GtkXText *xtext = GTK_XTEXT (widget);

	if (xtext->cursor_hand)
	{
		gtk_xtext_unrender_hilight (xtext);
		xtext->hilight_start = -1;
		xtext->hilight_end = -1;
		xtext->cursor_hand = FALSE;
		gdk_window_set_cursor (gtk_widget_get_window (widget), 0);
		xtext->hilight_ent = NULL;
	}

	if (xtext->cursor_resize)
	{
		gtk_xtext_unrender_hilight (xtext);
		xtext->hilight_start = -1;
		xtext->hilight_end = -1;
		xtext->cursor_resize = FALSE;
		gdk_window_set_cursor (gtk_widget_get_window (widget), 0);
		xtext->hilight_ent = NULL;
	}

	return FALSE;
}

/* check if we should mark time stamps, and if a redraw is needed */

static gboolean
gtk_xtext_check_mark_stamp (GtkXText *xtext, GdkModifierType mask)
{
	gboolean redraw = FALSE;

	if ((mask & GDK_SHIFT_MASK))
	{
		if (!xtext->mark_stamp)
		{
			redraw = TRUE;	/* must redraw all */
			xtext->mark_stamp = TRUE;
		}
	} else
	{
		if (xtext->mark_stamp)
		{
			redraw = TRUE;	/* must redraw all */
			xtext->mark_stamp = FALSE;
		}
	}
	return redraw;
}

static gboolean
gtk_xtext_motion_notify (GtkWidget * widget, GdkEventMotion * event)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	GdkModifierType mask;
	GtkAllocation allocation;
	int redraw, tmp, x, y, offset, len, line_x;
	char *word;
	textentry *word_ent;

	gdk_window_get_pointer (gtk_widget_get_window (widget), &x, &y, &mask);

	gtk_widget_get_allocation (widget, &allocation);

	if (xtext->moving_separator)
	{
		if (x < (3 * allocation.width) / 5 && x > 15)
		{
			tmp = xtext->buffer->indent;
			xtext->buffer->indent = x;
			gtk_xtext_fix_indent (xtext->buffer);
			if (tmp != xtext->buffer->indent)
			{
				gtk_xtext_recalc_widths (xtext->buffer, FALSE);
				if (xtext->buffer->scrollbar_down)
					gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj) -
													  gtk_adjustment_get_page_size (xtext->adj));
				if (!xtext->io_tag)
					xtext->io_tag = g_timeout_add (REFRESH_TIMEOUT,
																(GSourceFunc)
																gtk_xtext_adjustment_timeout,
																xtext);
			}
		}
		return FALSE;
	}

	if (xtext->button_down)
	{
		redraw = gtk_xtext_check_mark_stamp (xtext, mask);
		gtk_grab_add (widget);
		/*gdk_pointer_grab (gtk_widget_get_window (widget), TRUE,
									GDK_BUTTON_RELEASE_MASK |
									GDK_BUTTON_MOTION_MASK, NULL, NULL, 0);*/
		xtext->select_end_x = x;
		xtext->select_end_y = y;
		gtk_xtext_selection_update (xtext, event, y, !redraw);
		xtext->hilighting = TRUE;

		/* user has pressed or released SHIFT, must redraw entire selection */
		if (redraw)
		{
			xtext->force_stamp = TRUE;
			gtk_xtext_render_ents (xtext, xtext->buffer->last_ent_start,
										  xtext->buffer->last_ent_end);
			xtext->force_stamp = FALSE;
		}
		return FALSE;
	}

	if (xtext->separator && xtext->buffer->indent)
	{
		line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (line_x == x || line_x == x + 1 || line_x == x - 1)
		{
			if (!xtext->cursor_resize)
			{
				gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET(xtext)), xtext->resize_cursor);
				xtext->cursor_resize = TRUE;
			}
			return FALSE;
		}
	}

	if (xtext->urlcheck_function == NULL)
		return FALSE;

	word = gtk_xtext_get_word (xtext, x, y, &word_ent, &offset, &len);

	if (!word || (xtext->current_word != NULL &&
				  strcmp (word, xtext->current_word) != 0)) {
		if (xtext->current_word) {
			g_signal_emit (G_OBJECT (xtext), xtext_signals[WORD_LEAVE], 0, xtext->current_word);
			g_free (xtext->current_word);
			xtext->current_word = NULL;
		}
	}

	if (word)
	{
		if (xtext->urlcheck_function (GTK_WIDGET (xtext), word, len) > 0)
		{
			if (!xtext->cursor_hand ||
				 xtext->hilight_ent != word_ent ||
				 xtext->hilight_start != offset ||
				 xtext->hilight_end != offset + len)
			{
				if (!xtext->cursor_hand)
				{
					gdk_window_set_cursor (gtk_widget_get_window (GTK_WIDGET(xtext)), xtext->hand_cursor);
					xtext->cursor_hand = TRUE;
				}

				/* un-render the old hilight */
				if (xtext->hilight_ent)
					gtk_xtext_unrender_hilight (xtext);

				xtext->hilight_ent = word_ent;
				xtext->hilight_start = offset;
				xtext->hilight_end = offset + len;

				xtext->skip_border_fills = TRUE;
				xtext->render_hilights_only = TRUE;
				xtext->skip_stamp = TRUE;

				gtk_xtext_render_ents (xtext, word_ent, NULL);

				xtext->skip_border_fills = FALSE;
				xtext->render_hilights_only = FALSE;
				xtext->skip_stamp = FALSE;
			}

			if (!xtext->current_word ||
				strcmp (word, xtext->current_word) != 0) {
				xtext->current_word = g_strdup (word);
				g_signal_emit (G_OBJECT (xtext), xtext_signals[WORD_ENTER], 0, xtext->current_word);
			}

			return FALSE;
		}
	}

	gtk_xtext_leave_notify (widget, NULL);

	return FALSE;
}

static void
gtk_xtext_set_clip_owner (GtkWidget * xtext, GdkEventButton * event)
{
	if (GTK_XTEXT (xtext)->selection_buffer &&
		GTK_XTEXT (xtext)->selection_buffer != GTK_XTEXT (xtext)->buffer)
		gtk_xtext_selection_clear (GTK_XTEXT (xtext)->selection_buffer);

	GTK_XTEXT (xtext)->selection_buffer = GTK_XTEXT (xtext)->buffer;

	gtk_selection_owner_set (xtext, GDK_SELECTION_PRIMARY, event->time);
}

static void
gtk_xtext_unselect (GtkXText *xtext)
{
	xtext_buffer *buf = xtext->buffer;

	xtext->skip_border_fills = TRUE;
	xtext->skip_stamp = TRUE;

	xtext->jump_in_offset = buf->last_ent_start->mark_start;
	/* just a single ent was marked? */
	if (buf->last_ent_start == buf->last_ent_end)
	{
		xtext->jump_out_offset = buf->last_ent_start->mark_end;
		buf->last_ent_end = NULL;
	}

	gtk_xtext_selection_clear (xtext->buffer);

	/* FIXME: use jump_out on multi-line selects too! */
	gtk_xtext_render_ents (xtext, buf->last_ent_start, buf->last_ent_end);

	xtext->jump_in_offset = 0;
	xtext->jump_out_offset = 0;

	xtext->skip_border_fills = FALSE;
	xtext->skip_stamp = FALSE;

	xtext->buffer->last_ent_start = NULL;
	xtext->buffer->last_ent_end = NULL;
}

static gboolean
gtk_xtext_button_release (GtkWidget * widget, GdkEventButton * event)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	GtkAllocation allocation;
	char *word;
	int old;

	gtk_widget_get_allocation (widget, &allocation);

	if (xtext->moving_separator)
	{
		xtext->moving_separator = FALSE;
		old = xtext->buffer->indent;
		if (event->x < (4 * allocation.width) / 5 && event->x > 15)
			xtext->buffer->indent = event->x;
		gtk_xtext_fix_indent (xtext->buffer);
		if (xtext->buffer->indent != old)
		{
			gtk_xtext_recalc_widths (xtext->buffer, FALSE);
			gtk_xtext_adjustment_set (xtext->buffer, TRUE);
			gtk_xtext_render_page (xtext);
		} else
			gtk_xtext_draw_sep (xtext, -1);
		return FALSE;
	}

	if (xtext->word_or_line_select)
	{
		xtext->word_or_line_select = FALSE;
		xtext->button_down = FALSE;
		return FALSE;
	}

	if (event->button == 1)
	{
		xtext->button_down = FALSE;

		gtk_grab_remove (widget);
		/*gdk_pointer_ungrab (0);*/
		if (xtext->buffer->last_ent_start)
			gtk_xtext_set_clip_owner (GTK_WIDGET (xtext), event);

		if (xtext->select_start_x == event->x &&
			 xtext->select_start_y == event->y &&
			 xtext->buffer->last_ent_start)
		{
			gtk_xtext_unselect (xtext);
			xtext->mark_stamp = FALSE;
			return FALSE;
		}

		if (!xtext->hilighting)
		{
			word = gtk_xtext_get_word (xtext, event->x, event->y, 0, 0, 0);
			g_signal_emit (G_OBJECT (xtext), xtext_signals[WORD_CLICK], 0, word ? word : NULL, event);
		} else
		{
			xtext->hilighting = FALSE;
		}
	}

	return FALSE;
}

static gboolean
gtk_xtext_button_press (GtkWidget * widget, GdkEventButton * event)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	GdkModifierType mask;
	textentry *ent;
	char *word;
	int line_x, x, y, offset, len;

	gdk_window_get_pointer (gtk_widget_get_window (widget), &x, &y, &mask);

	if (event->button == 3 || event->button == 2) /* right/middle click */
	{
		word = gtk_xtext_get_word (xtext, x, y, 0, 0, 0);
		if (word)
		{
			g_signal_emit (G_OBJECT (xtext), xtext_signals[WORD_CLICK], 0,
								word, event);
		} else
			g_signal_emit (G_OBJECT (xtext), xtext_signals[WORD_CLICK], 0,
								"", event);
		return TRUE;
	}

	if (event->button != 1)		  /* we only want left button */
		return FALSE;

	if (event->type == GDK_2BUTTON_PRESS)	/* WORD select */
	{
		gtk_xtext_check_mark_stamp (xtext, mask);
		if (gtk_xtext_get_word (xtext, x, y, &ent, &offset, &len))
		{
			if (len == 0)
				return FALSE;
			gtk_xtext_selection_clear (xtext->buffer);
			ent->mark_start = offset;
			ent->mark_end = offset + len;
			gtk_xtext_selection_render (xtext, ent, offset, ent, offset + len);
			xtext->word_or_line_select = TRUE;
			gtk_xtext_set_clip_owner (GTK_WIDGET (xtext), event);
		}

		return TRUE;
	}

	if (event->type == GDK_3BUTTON_PRESS)	/* LINE select */
	{
		gtk_xtext_check_mark_stamp (xtext, mask);
		if (gtk_xtext_get_word (xtext, x, y, &ent, 0, 0))
		{
			gtk_xtext_selection_clear (xtext->buffer);
			ent->mark_start = 0;
			ent->mark_end = ent->str_len;
			gtk_xtext_selection_render (xtext, ent, 0, ent, ent->str_len);
			xtext->word_or_line_select = TRUE;
			gtk_xtext_set_clip_owner (GTK_WIDGET (xtext), event);
		}

		return TRUE;
	}

	/* check if it was a separator-bar click */
	if (xtext->separator && xtext->buffer->indent)
	{
		line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (line_x == x || line_x == x + 1 || line_x == x - 1)
		{
			xtext->moving_separator = TRUE;
			/* draw the separator line */
			gtk_xtext_draw_sep (xtext, -1);
			return TRUE;
		}
	}

	xtext->button_down = TRUE;
	xtext->select_start_x = x;
	xtext->select_start_y = y;
	xtext->select_start_adj = gtk_adjustment_get_value (xtext->adj);

	return TRUE;
}

/* another program has claimed the selection */

static gboolean
gtk_xtext_selection_kill (GtkXText *xtext, GdkEventSelection *event)
{
	if (xtext->buffer->last_ent_start)
		gtk_xtext_unselect (xtext);
	return TRUE;
}

static char *
gtk_xtext_selection_get_text (GtkXText *xtext, int *len_ret)
{
	textentry *ent;
	char *txt;
	char *pos;
	char *stripped;
	int len;
	int first = TRUE;
	xtext_buffer *buf;

	buf = xtext->selection_buffer;
	if (!buf)
		return NULL;

	/* first find out how much we need to malloc ... */
	len = 0;
	ent = buf->last_ent_start;
	while (ent)
	{
		if (ent->mark_start != -1)
		{
			/* include timestamp? */
			if (ent->mark_start == 0 && xtext->mark_stamp)
			{
				char *time_str;
				int stamp_size = xtext_get_stamp_str (ent->stamp, &time_str);
				g_free (time_str);
				len += stamp_size;
			}

			if (ent->mark_end - ent->mark_start > 0)
				len += (ent->mark_end - ent->mark_start) + 1;
			else
				len++;
		}
		if (ent == buf->last_ent_end)
			break;
		ent = ent->next;
	}

	if (len < 1)
		return NULL;

	/* now allocate mem and copy buffer */
	pos = txt = malloc (len);
	ent = buf->last_ent_start;
	while (ent)
	{
		if (ent->mark_start != -1)
		{
			if (!first)
			{
				*pos = '\n';
				pos++;
			}
			first = FALSE;
			if (ent->mark_end - ent->mark_start > 0)
			{
				/* include timestamp? */
				if (ent->mark_start == 0 && xtext->mark_stamp)
				{
					char *time_str;
					int stamp_size = xtext_get_stamp_str (ent->stamp, &time_str);
					memcpy (pos, time_str, stamp_size);
					g_free (time_str);
					pos += stamp_size;
				}

				memcpy (pos, ent->str + ent->mark_start,
						  ent->mark_end - ent->mark_start);
				pos += ent->mark_end - ent->mark_start;
			}
		}
		if (ent == buf->last_ent_end)
			break;
		ent = ent->next;
	}
	*pos = 0;

	if (xtext->color_paste)
	{
		/*stripped = gtk_xtext_conv_color (txt, strlen (txt), &len);*/
		stripped = txt;
		len = strlen (txt);
	} else
	{
		stripped = (char *) gtk_xtext_strip_color ((unsigned char *) txt, strlen (txt), NULL, &len, 0, FALSE);
		free (txt);
	}

	*len_ret = len;
	return stripped;
}

/* another program is asking for our selection */

static void
gtk_xtext_selection_get (GtkWidget * widget,
			 GtkSelectionData * selection_data_ptr,
			 guint info, guint time)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	char *stripped;
	guchar *new_text;
	int len;
	gsize glen;

	stripped = gtk_xtext_selection_get_text (xtext, &len);
	if (!stripped)
		return;

	switch (info)
	{
	case TARGET_UTF8_STRING:
		/* it's already in utf8 */
		gtk_selection_data_set_text (selection_data_ptr, stripped, len);
		break;
	case TARGET_TEXT:
	case TARGET_COMPOUND_TEXT:
		{
#if 0
			GdkAtom encoding;
			gint format;
			gint new_length;

			gdk_string_to_compound_text_for_display (
							gdk_window_get_display (gtk_widget_get_window (widget)),
							stripped, &encoding, &format, &new_text,
							&new_length);
			gtk_selection_data_set (selection_data_ptr, encoding, format,
						new_text, new_length);
			gdk_free_compound_text (new_text);
#endif
		}
		break;
	default:
		new_text = (guchar *) g_locale_from_utf8 (stripped, len, NULL, &glen, NULL);
		gtk_selection_data_set (selection_data_ptr, GDK_SELECTION_TYPE_STRING,
										8, new_text, glen);
		g_free (new_text);
	}

	free (stripped);
}

static gboolean
gtk_xtext_scroll (GtkWidget *widget, GdkEventScroll *event)
{
	GtkXText *xtext = GTK_XTEXT (widget);
	gfloat new_value;

	if (event->direction == GDK_SCROLL_UP)		/* mouse wheel pageUp */
	{
		new_value = gtk_adjustment_get_value (xtext->adj) - (gtk_adjustment_get_page_increment (xtext->adj) / 10);
		if (new_value < gtk_adjustment_get_lower (xtext->adj))
			new_value = gtk_adjustment_get_lower (xtext->adj);
		gtk_adjustment_set_value (xtext->adj, new_value);
	}
	else if (event->direction == GDK_SCROLL_DOWN)	/* mouse wheel pageDn */
	{
		new_value = gtk_adjustment_get_value (xtext->adj) + (gtk_adjustment_get_page_increment (xtext->adj) / 10);
		if (new_value > (gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj)))
			new_value = gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj);
		gtk_adjustment_set_value (xtext->adj, new_value);
	}

	return FALSE;
}

static void
gtk_xtext_class_init (GtkXTextClass * class)
{
	GtkWidgetClass *widget_class;
	GtkXTextClass *xtext_class;

	widget_class = (GtkWidgetClass *) class;
	xtext_class = (GtkXTextClass *) class;

	parent_class = g_type_class_peek (gtk_widget_get_type ());

	xtext_signals[WORD_CLICK] =
		g_signal_new ("word_click",
							G_TYPE_FROM_CLASS (widget_class),
							G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
							G_STRUCT_OFFSET (GtkXTextClass, word_click),
							NULL, NULL,
							xg_marshal_VOID__POINTER_POINTER,
							G_TYPE_NONE,
							2, G_TYPE_POINTER, G_TYPE_POINTER);
	xtext_signals[WORD_ENTER] =
		g_signal_new ("word_enter",
					  G_TYPE_FROM_CLASS (widget_class),
					  G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
					  G_STRUCT_OFFSET (GtkXTextClass, word_enter),
					  NULL, NULL,
					  xg_marshal_VOID__POINTER,
					  G_TYPE_NONE,
					  1, G_TYPE_POINTER);
	xtext_signals[WORD_LEAVE] =
		g_signal_new ("word_leave",
					  G_TYPE_FROM_CLASS (widget_class),
					  G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
					  G_STRUCT_OFFSET (GtkXTextClass, word_leave),
					  NULL, NULL,
					  xg_marshal_VOID__POINTER,
					  G_TYPE_NONE,
					  1, G_TYPE_POINTER);
	widget_class->destroy = gtk_xtext_destroy;

	widget_class->realize = gtk_xtext_realize;
	widget_class->unrealize = gtk_xtext_unrealize;
	widget_class->get_preferred_width = gtk_xtext_get_preferred_width;
	widget_class->get_preferred_height = gtk_xtext_get_preferred_height;
	widget_class->size_allocate = gtk_xtext_size_allocate;
	widget_class->button_press_event = gtk_xtext_button_press;
	widget_class->button_release_event = gtk_xtext_button_release;
	widget_class->motion_notify_event = gtk_xtext_motion_notify;
	widget_class->selection_clear_event = (void *)gtk_xtext_selection_kill;
	widget_class->selection_get = gtk_xtext_selection_get;
	widget_class->draw = gtk_xtext_draw;
	widget_class->scroll_event = gtk_xtext_scroll;
	widget_class->leave_notify_event = gtk_xtext_leave_notify;

	xtext_class->word_click = NULL;
}

GType
gtk_xtext_get_type (void)
{
	static GType xtext_type = 0;

	if (!xtext_type)
	{
		static const GTypeInfo xtext_info =
		{
			sizeof (GtkXTextClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gtk_xtext_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GtkXText),
			0,		/* n_preallocs */
			(GInstanceInitFunc) gtk_xtext_init,
		};

		xtext_type = g_type_register_static (GTK_TYPE_WIDGET, "GtkXText",
														 &xtext_info, 0);
	}

	return xtext_type;
}

/* strip MIRC colors and other attribs. */

/* CL: needs to strip hidden when called by gtk_xtext_text_width, but not when copying text */

static unsigned char *
gtk_xtext_strip_color (unsigned char *text, int len, unsigned char *outbuf,
							  int *newlen, int *mb_ret, int strip_hidden)
{
	int i = 0;
	int rcol = 0, bgcol = 0;
	int hidden = FALSE;
	unsigned char *new_str;
	int mb = FALSE;

	if (outbuf == NULL)
		new_str = malloc (len + 2);
	else
		new_str = outbuf;

	while (len > 0)
	{
		if (*text >= 128)
			mb = TRUE;

		if (rcol > 0 && (isdigit (*text) || (*text == ',' && isdigit (text[1]) && !bgcol)))
		{
			if (text[1] != ',') rcol--;
			if (*text == ',')
			{
				rcol = 2;
				bgcol = 1;
			}
		} else
		{
			rcol = bgcol = 0;
			switch (*text)
			{
			case ATTR_COLOR:
				rcol = 2;
				break;
			case ATTR_BEEP:
			case ATTR_RESET:
			case ATTR_REVERSE:
			case ATTR_BOLD:
			case ATTR_UNDERLINE:
			case ATTR_ITALICS:
				break;
			case ATTR_HIDDEN:
				hidden = !hidden;
				break;
			default:
				if (!(hidden && strip_hidden))
					new_str[i++] = *text;
			}
		}
		text++;
		len--;
	}

	new_str[i] = 0;

	if (newlen != NULL)
		*newlen = i;

	if (mb_ret != NULL)
		*mb_ret = mb;

	return new_str;
}

/* gives width of a string, excluding the mIRC codes */

static int
gtk_xtext_text_width (GtkXText *xtext, unsigned char *text, int len,
							 int *mb_ret)
{
	unsigned char *new_buf;
	int new_len, mb;

	new_buf = gtk_xtext_strip_color (text, len, xtext->scratch_buffer,
												&new_len, &mb, !xtext->ignore_hidden);

	if (mb_ret)
		*mb_ret = mb;

	return backend_get_text_width (xtext, new_buf, new_len, mb);
}

/* actually draw text to screen (one run with the same color/attribs) */

static int
gtk_xtext_render_flush (GtkXText * xtext, int x, int y, unsigned char *str,
								int len, int is_mb)
{
	int str_width, dofill;
	int dest_x, dest_y;
    cairo_t *cr;

	dest_x = dest_y = 0;

	if (xtext->dont_render || len < 1 || xtext->hidden)
		return 0;

	str_width = backend_get_text_width (xtext, str, len, is_mb);

	if (xtext->dont_render2 || str_width < 1)
		return str_width;

	/* roll-your-own clipping (avoiding XftDrawString is always good!) */
	if (x > xtext->clip_x2 || x + str_width < xtext->clip_x)
		return str_width;
	if (y - xtext->font->ascent > xtext->clip_y2 || (y - xtext->font->ascent) + xtext->fontsize < xtext->clip_y)
		return str_width;

	if (xtext->render_hilights_only)
	{
		if (!xtext->in_hilight)	/* is it a hilight prefix? */
			return str_width;
		if (!xtext->un_hilight)	/* doing a hilight? no need to draw the text */
			goto dounder;
	}

	dofill = TRUE;

	backend_draw_text (xtext, dofill, x, y, (char *) str, len, str_width, is_mb);

	if (xtext->underline)
	{
dounder:
		y++;
		dest_x = x;

        /* The cairo_t context has to be created here due to spaghetti-goto jump */
		cr = gdk_cairo_create (gtk_widget_get_window (&xtext->widget));
        xtext_draw_line (xtext, cr, &xtext->text_fg, 
                dest_x + 1, y + 1, dest_x + str_width - 1, y + 1);
        cairo_destroy (cr);
    }

	return str_width;
}

static void
gtk_xtext_reset (GtkXText * xtext, int mark, int attribs)
{
	if (attribs)
	{
		xtext->underline = FALSE;
		xtext->bold = FALSE;
		xtext->italics = FALSE;
		xtext->hidden = FALSE;
	}
	if (!mark)
	{
		xtext->backcolor = FALSE;
		if (xtext->col_fore != XTEXT_FG)
            xtext->text_fg = xtext_palette_lookup (xtext, XTEXT_FG);
		if (xtext->col_back != XTEXT_BG)
            xtext->text_bg = xtext_palette_lookup (xtext, XTEXT_BG);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
	xtext->parsing_color = FALSE;
	xtext->parsing_backcolor = FALSE;
	xtext->nc = 0;
}

/* render a single line, which WONT wrap, and parse mIRC colors */

static int
gtk_xtext_render_str (GtkXText * xtext, int y, textentry * ent,
							 unsigned char *str, int len, int win_width, int indent,
							 int line, int left_only, int *x_size_ret)
{
	int i = 0, x = indent, j = 0;
	unsigned char *pstr = str;
	int col_num, tmp;
	int offset;
	int mark = FALSE;
	int ret = 1;

	xtext->in_hilight = FALSE;

	offset = str - ent->str;

	if (line < 255 && line >= 0)
		xtext->buffer->grid_offset[line] = offset;

	if (ent->mark_start != -1 &&
		 ent->mark_start <= i + offset && ent->mark_end > i + offset)
	{
        xtext->text_bg = xtext_palette_lookup (xtext, XTEXT_MARK_BG);
        xtext->text_fg = xtext_palette_lookup (xtext, XTEXT_MARK_FG);
		xtext->backcolor = TRUE;
		mark = TRUE;
	}
	if (xtext->hilight_ent == ent &&
		 xtext->hilight_start <= i + offset && xtext->hilight_end > i + offset)
	{
		if (!xtext->un_hilight)
		{
			xtext->underline = TRUE;
		}
		xtext->in_hilight = TRUE;
	}

	if (!xtext->skip_border_fills && !xtext->dont_render)
	{
		/* draw background to the left of the text */
		if (str == ent->str && indent > MARGIN && xtext->buffer->time_stamp)
		{
			/* don't overwrite the timestamp */
			if (indent > xtext->stamp_width)
			{
				xtext_draw_bg (xtext, xtext->stamp_width, y - xtext->font->ascent,
									indent - xtext->stamp_width, xtext->fontsize);
			}
		} else
		{
			/* fill the indent area with background gc */
			if (indent >= xtext->clip_x)
			{
				xtext_draw_bg (xtext, 0, y - xtext->font->ascent,
									MIN (indent, xtext->clip_x2), xtext->fontsize);
			}
		}
	}

	if (xtext->jump_in_offset > 0 && offset < xtext->jump_in_offset)
		xtext->dont_render2 = TRUE;

	while (i < len)
	{

		if (xtext->hilight_ent == ent && xtext->hilight_start == (i + offset))
		{
			x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
			pstr += j;
			j = 0;
			if (!xtext->un_hilight)
			{
				xtext->underline = TRUE;
			}

			xtext->in_hilight = TRUE;
		}

		if ((xtext->parsing_color && isdigit (str[i]) && xtext->nc < 2) ||
			 (xtext->parsing_color && str[i] == ',' && isdigit (str[i+1]) && xtext->nc < 3 && !xtext->parsing_backcolor))
		{
			pstr++;
			if (str[i] == ',')
			{
				xtext->parsing_backcolor = TRUE;
				if (xtext->nc)
				{
					xtext->num[xtext->nc] = 0;
					xtext->nc = 0;
					col_num = atoi (xtext->num);
					if (col_num == 99)	/* mIRC lameness */
						col_num = XTEXT_FG;
					else
						col_num = col_num % XTEXT_MIRC_COLS;
					xtext->col_fore = col_num;
					if (!mark)
                        xtext->text_fg = xtext_palette_lookup (xtext, col_num);
				}
			} else
			{
				xtext->num[xtext->nc] = str[i];
				if (xtext->nc < 7)
					xtext->nc++;
			}
		} else
		{
			if (xtext->parsing_color)
			{
				xtext->parsing_color = FALSE;
				if (xtext->nc)
				{
					xtext->num[xtext->nc] = 0;
					xtext->nc = 0;
					col_num = atoi (xtext->num);
					if (xtext->parsing_backcolor)
					{
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_BG;
						else
							col_num = col_num % XTEXT_MIRC_COLS;
						if (col_num == XTEXT_BG)
							xtext->backcolor = FALSE;
						else
							xtext->backcolor = TRUE;
						if (!mark)
                            xtext->text_bg = xtext_palette_lookup (xtext, col_num);
						xtext->col_back = col_num;
					} else
					{
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_FG;
						else
							col_num = col_num % XTEXT_MIRC_COLS;
						if (!mark)
                            xtext->text_fg = xtext_palette_lookup (xtext, col_num);
						xtext->col_fore = col_num;
					}
					xtext->parsing_backcolor = FALSE;
				} else
				{
					/* got a \003<non-digit>... i.e. reset colors */
					x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
					pstr += j;
					j = 0;
					gtk_xtext_reset (xtext, mark, FALSE);
				}
			}

			switch (str[i])
			{
			case '\n':
			/*case ATTR_BEEP:*/
				break;
			case ATTR_REVERSE:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				pstr += j + 1;
				j = 0;
				tmp = xtext->col_fore;
				xtext->col_fore = xtext->col_back;
				xtext->col_back = tmp;
				if (!mark)
				{
                    xtext->text_fg = xtext_palette_lookup (xtext, xtext->col_fore);
                    xtext->text_bg = xtext_palette_lookup (xtext, xtext->col_back);
				}
				if (xtext->col_back != XTEXT_BG)
					xtext->backcolor = TRUE;
				else
					xtext->backcolor = FALSE;
				break;
			case ATTR_BOLD:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				xtext->bold = !xtext->bold;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_UNDERLINE:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				xtext->underline = !xtext->underline;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_ITALICS:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				xtext->italics = !xtext->italics;
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_HIDDEN:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				xtext->hidden = (!xtext->hidden) & (!xtext->ignore_hidden);
				pstr += j + 1;
				j = 0;
				break;
			case ATTR_RESET:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				pstr += j + 1;
				j = 0;
				gtk_xtext_reset (xtext, mark, !xtext->in_hilight);
				break;
			case ATTR_COLOR:
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				xtext->parsing_color = TRUE;
				pstr += j + 1;
				j = 0;
				break;
			default:
				tmp = charlen (str + i);
				/* invalid utf8 safe guard */
				if (tmp + i > len)
					tmp = len - i;
				j += tmp;	/* move to the next utf8 char */
			}
		}
		i += charlen (str + i);	/* move to the next utf8 char */
		/* invalid utf8 safe guard */
		if (i > len)
			i = len;

		/* Separate the left part, the space and the right part
		   into separate runs, and reset bidi state inbetween.
		   Perform this only on the first line of the message.
                */
		if (offset == 0)
		{
			/* we've reached the end of the left part? */
			if ((pstr-str)+j == ent->left_len)
			{
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				pstr += j;
				j = 0;
			}
			else if ((pstr-str)+j == ent->left_len+1)
			{
				x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
				pstr += j;
				j = 0;
			}
		}

		/* have we been told to stop rendering at this point? */
		if (xtext->jump_out_offset > 0 && xtext->jump_out_offset <= (i + offset))
		{
			gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
			ret = 0;	/* skip the rest of the lines, we're done. */
			j = 0;
			break;
		}

		if (xtext->jump_in_offset > 0 && xtext->jump_in_offset == (i + offset))
		{
			x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
			pstr += j;
			j = 0;
			xtext->dont_render2 = FALSE;
		}

		if (xtext->hilight_ent == ent && xtext->hilight_end == (i + offset))
		{
			x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
			pstr += j;
			j = 0;
			xtext->underline = FALSE;
			
			xtext->in_hilight = FALSE;
			if (xtext->render_hilights_only)
			{
				/* stop drawing this ent */
				ret = 0;
				break;
			}
		}

		if (!mark && ent->mark_start == (i + offset))
		{
			x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
			pstr += j;
			j = 0;
            xtext->text_bg = xtext_palette_lookup (xtext, XTEXT_MARK_BG);
            xtext->text_fg = xtext_palette_lookup (xtext, XTEXT_MARK_FG);
			xtext->backcolor = TRUE;
			mark = TRUE;
		}

		if (mark && ent->mark_end == (i + offset))
		{
			x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);
			pstr += j;
			j = 0;
            xtext->text_bg = xtext_palette_lookup (xtext, xtext->col_back);
            xtext->text_fg = xtext_palette_lookup (xtext, xtext->col_fore);
			if (xtext->col_back != XTEXT_BG)
				xtext->backcolor = TRUE;
			else
				xtext->backcolor = FALSE;
			mark = FALSE;
		}

	}

	if (j)
		x += gtk_xtext_render_flush (xtext, x, y, pstr, j, ent->mb);

	if (mark)
	{
        xtext->text_bg = xtext_palette_lookup (xtext, xtext->col_back);
        xtext->text_fg = xtext_palette_lookup (xtext, xtext->col_fore);
		if (xtext->col_back != XTEXT_BG)
			xtext->backcolor = TRUE;
		else
			xtext->backcolor = FALSE;
	}

	/* draw background to the right of the text */
	if (!left_only && !xtext->dont_render)
	{
		/* draw separator now so it doesn't appear to flicker */
		gtk_xtext_draw_sep (xtext, y - xtext->font->ascent);
		if (!xtext->skip_border_fills && xtext->clip_x2 >= x)
		{
			int xx = MAX (x, xtext->clip_x);

			xtext_draw_bg (xtext,
								xx,	/* x */
								y - xtext->font->ascent, /* y */
				MIN (xtext->clip_x2 - xx, (win_width + MARGIN) - xx), /* width */
								xtext->fontsize);		/* height */
		}
	}

	xtext->dont_render2 = FALSE;

	/* return how much we drew in the x direction */
	if (x_size_ret)
		*x_size_ret = x - indent;

	return ret;
}

/* walk through str until this line doesn't fit anymore */

static int
find_next_wrap (GtkXText * xtext, textentry * ent, unsigned char *str,
					 int win_width, int indent)
{
	unsigned char *last_space = str;
	unsigned char *orig_str = str;
	int str_width = indent;
	int rcol = 0, bgcol = 0;
	int hidden = FALSE;
	int mbl;
	int char_width;
	int ret;
	int limit_offset = 0;

	/* single liners */
	if (win_width >= ent->str_width + ent->indent)
		return ent->str_len;

	/* it does happen! */
	if (win_width < 1)
	{
		ret = ent->str_len - (str - ent->str);
		goto done;
	}

	while (1)
	{
		if (rcol > 0 && (isdigit (*str) || (*str == ',' && isdigit (str[1]) && !bgcol)))
		{
			if (str[1] != ',') rcol--;
			if (*str == ',')
			{
				rcol = 2;
				bgcol = 1;
			}
			limit_offset++;
			str++;
		} else
		{
			rcol = bgcol = 0;
			switch (*str)
			{
			case ATTR_COLOR:
				rcol = 2;
			case ATTR_BEEP:
			case ATTR_RESET:
			case ATTR_REVERSE:
			case ATTR_BOLD:
			case ATTR_UNDERLINE:
			case ATTR_ITALICS:
				limit_offset++;
				str++;
				break;
			case ATTR_HIDDEN:
				if (xtext->ignore_hidden)
					goto def;
				hidden = !hidden;
				limit_offset++;
				str++;
				break;
			default:
			def:
				char_width = backend_get_char_width (xtext, str, &mbl);
				if (!hidden) str_width += char_width;
				if (str_width > win_width)
				{
					if (xtext->wordwrap)
					{
						if (str - last_space > WORDWRAP_LIMIT + limit_offset)
							ret = str - orig_str; /* fall back to character wrap */
						else
						{
							if (*last_space == ' ')
								last_space++;
							ret = last_space - orig_str;
							if (ret == 0) /* fall back to character wrap */
								ret = str - orig_str;
						}
						goto done;
					}
					ret = str - orig_str;
					goto done;
				}

				/* keep a record of the last space, for wordwrapping */
				if (is_del (*str))
				{
					last_space = str;
					limit_offset = 0;
				}

				/* progress to the next char */
				str += mbl;

			}
		}

		if (str >= ent->str + ent->str_len)
		{
			ret = str - orig_str;
			goto done;
		}
	}

done:

	/* must make progress */
	if (ret < 1)
		ret = 1;

	return ret;
}

/* find the offset, in bytes, that wrap number 'line' starts at */

static int
gtk_xtext_find_subline (GtkXText *xtext, textentry *ent, int line)
{
	int win_width;
	unsigned char *str;
	int indent, str_pos, line_pos, len;

	if (ent->lines_taken < 2 || line < 1)
		return 0;

	/* we record the first 4 lines' wraps, so take a shortcut */
	if (line <= RECORD_WRAPS)
		return ent->wrap_offset[line - 1];

	win_width = gdk_window_get_width (gtk_widget_get_window (GTK_WIDGET(xtext)));
	win_width -= MARGIN;

/*	indent = ent->indent;
	str = ent->str;
	line_pos = str_pos = 0;*/

	/* start from the last recorded wrap, and move forward */
	indent = xtext->buffer->indent;
	str_pos = ent->wrap_offset[RECORD_WRAPS-1];
	str = str_pos + ent->str;
	line_pos = RECORD_WRAPS;

	do
	{
		len = find_next_wrap (xtext, ent, str, win_width, indent);
		indent = xtext->buffer->indent;
		str += len;
		str_pos += len;
		line_pos++;
		if (line_pos >= line)
			return str_pos;
	}
	while (str < ent->str + ent->str_len);

	return 0;
}

/* horrible hack for drawing time stamps */

static void
gtk_xtext_render_stamp (GtkXText * xtext, textentry * ent,
								char *text, int len, int line, int win_width)
{
	textentry tmp_ent;
	int jo, ji, hs;
	int xsize, y;

	/* trashing ent here, so make a backup first */
	memcpy (&tmp_ent, ent, sizeof (tmp_ent));
	ent->mb = TRUE;	/* make non-english days of the week work */
	jo = xtext->jump_out_offset;	/* back these up */
	ji = xtext->jump_in_offset;
	hs = xtext->hilight_start;
	xtext->jump_out_offset = 0;
	xtext->jump_in_offset = 0;
	xtext->hilight_start = 0xffff;	/* temp disable */

	if (xtext->mark_stamp)
	{
		/* if this line is marked, mark this stamp too */
		if (ent->mark_start == 0)	
		{
			ent->mark_start = 0;
			ent->mark_end = len;
		}
		else
		{
			ent->mark_start = -1;
			ent->mark_end = -1;
		}
		ent->str = (guchar *) text;
	}

	y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
	gtk_xtext_render_str (xtext, y, ent, (guchar *) text, len,
								 win_width, 2, line, TRUE, &xsize);

	/* restore everything back to how it was */
	memcpy (ent, &tmp_ent, sizeof (tmp_ent));
	xtext->jump_out_offset = jo;
	xtext->jump_in_offset = ji;
	xtext->hilight_start = hs;

	/* with a non-fixed-width font, sometimes we don't draw enough
		background i.e. when this stamp is shorter than xtext->stamp_width */
	xsize += MARGIN;
	if (xsize < xtext->stamp_width)
	{
		y -= xtext->font->ascent;
		xtext_draw_bg (xtext,
							xsize,	/* x */
							y,			/* y */
							xtext->stamp_width - xsize,	/* width */
							xtext->fontsize					/* height */);
	}
}

/* render a single line, which may wrap to more lines */

static int
gtk_xtext_render_line (GtkXText * xtext, textentry * ent, int line,
							  int lines_max, int subline, int win_width)
{
	unsigned char *str;
	int indent, taken, entline, len, y, start_subline;

	entline = taken = 0;
	str = ent->str;
	indent = ent->indent;
	start_subline = subline;

	/* draw the timestamp */
	if (xtext->auto_indent && xtext->buffer->time_stamp &&
		 (!xtext->skip_stamp || xtext->mark_stamp || xtext->force_stamp))
	{
		char *time_str;
		int len;

		len = xtext_get_stamp_str (ent->stamp, &time_str);
		gtk_xtext_render_stamp (xtext, ent, time_str, len, line, win_width);
		g_free (time_str);
	}

	/* draw each line one by one */
	do
	{
		/* if it's one of the first 4 wraps, we don't need to calculate it, it's
			recorded in ->wrap_offset. This saves us a loop. */
		if (entline < RECORD_WRAPS)
		{
			if (ent->lines_taken < 2)
				len = ent->str_len;
			else
			{
				if (entline > 0)
					len = ent->wrap_offset[entline] - ent->wrap_offset[entline-1];
				else
					len = ent->wrap_offset[0];
			}
		} else
			len = find_next_wrap (xtext, ent, str, win_width, indent);

		entline++;

		y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
		if (!subline)
		{
			if (!gtk_xtext_render_str (xtext, y, ent, str, len, win_width,
												indent, line, FALSE, NULL))
			{
				/* small optimization */
				gtk_xtext_draw_marker (xtext, ent, y - xtext->fontsize * (taken + start_subline + 1));
				return ent->lines_taken - subline;
			}
		} else
		{
			xtext->dont_render = TRUE;
			gtk_xtext_render_str (xtext, y, ent, str, len, win_width,
										 indent, line, FALSE, NULL);
			xtext->dont_render = FALSE;
			subline--;
			line--;
			taken--;
		}

		indent = xtext->buffer->indent;
		line++;
		taken++;
		str += len;

		if (line >= lines_max)
			break;

	}
	while (str < ent->str + ent->str_len);

	gtk_xtext_draw_marker (xtext, ent, y - xtext->fontsize * (taken + start_subline));

	/*gtk_widget_queue_draw (GTK_WIDGET(xtext));*/

	return taken;
}

void
gtk_xtext_set_palette (GtkXText * xtext, GdkColor palette[])
{
    memcpy(&xtext->palette, palette, XTEXT_COLS * sizeof(GdkColor));

	if (gtk_widget_get_realized (GTK_WIDGET(xtext)))
	{
        xtext->text_fg   = xtext_palette_lookup (xtext, XTEXT_FG);
        xtext->text_bg   = xtext_palette_lookup (xtext, XTEXT_BG);

		xtext->marker_gc = xtext_palette_lookup (xtext, XTEXT_MARKER);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
}

static void
gtk_xtext_fix_indent (xtext_buffer *buf)
{
	int j;

	/* make indent a multiple of the space width */
	if (buf->indent && buf->xtext->space_width)
	{
		j = 0;
		while (j < buf->indent)
		{
			j += buf->xtext->space_width;
		}
		buf->indent = j;
	}
}

static void
gtk_xtext_recalc_widths (xtext_buffer *buf, int do_str_width)
{
	textentry *ent;

	/* since we have a new font, we have to recalc the text widths */
	ent = buf->text_first;
	while (ent)
	{
		if (do_str_width)
		{
			ent->str_width = gtk_xtext_text_width (buf->xtext, ent->str,
														ent->str_len, NULL);
		}
		if (ent->left_len != -1)
		{
			ent->indent =
				(buf->indent -
				 gtk_xtext_text_width (buf->xtext, ent->str,
										ent->left_len, NULL)) - buf->xtext->space_width;
			if (ent->indent < MARGIN)
				ent->indent = MARGIN;
		}
		ent = ent->next;
	}

	gtk_xtext_calc_lines (buf, FALSE);
}

int
gtk_xtext_set_font (GtkXText *xtext, char *name)
{
	int i;
	unsigned char c;

	if (xtext->font)
		backend_font_close (xtext);

	/* realize now, so that font_open has a XDisplay */
	gtk_widget_realize (GTK_WIDGET (xtext));

	backend_font_open (xtext, name);
	if (xtext->font == NULL)
		return FALSE;

	/* measure the width of every char;  only the ASCII ones for XFT */
	for (i = 0; i < sizeof(xtext->fontwidth)/sizeof(xtext->fontwidth[0]); i++)
	{
		c = i;
		xtext->fontwidth[i] = backend_get_text_width (xtext, &c, 1, TRUE);
	}
	xtext->space_width = xtext->fontwidth[' '];
	xtext->fontsize = xtext->font->ascent + xtext->font->descent;
	{
		char *time_str;
		int stamp_size = xtext_get_stamp_str (time(0), &time_str);
		xtext->stamp_width =
			gtk_xtext_text_width (xtext, (guchar *) time_str, stamp_size, NULL) + MARGIN;
		g_free (time_str);
	}

	gtk_xtext_fix_indent (xtext->buffer);

	if (gtk_widget_get_realized (GTK_WIDGET(xtext)))
		gtk_xtext_recalc_widths (xtext->buffer, TRUE);

	return TRUE;
}

void
gtk_xtext_save (GtkXText * xtext, int fh)
{
	textentry *ent;
	int newlen;
	unsigned char *buf;

	ent = xtext->buffer->text_first;
	while (ent)
	{
		buf = gtk_xtext_strip_color (ent->str, ent->str_len, NULL,
											  &newlen, NULL, FALSE);
		write (fh, buf, newlen);
		write (fh, "\n", 1);
		free (buf);
		ent = ent->next;
	}
}

/* count how many lines 'ent' will take (with wraps) */

static int
gtk_xtext_lines_taken (xtext_buffer *buf, textentry * ent)
{
	unsigned char *str;
	int indent, taken, len;
	int win_width;

	win_width = buf->window_width - MARGIN;

	if (ent->str_width + ent->indent < win_width)
		return 1;

	indent = ent->indent;
	str = ent->str;
	taken = 0;

	do
	{
		len = find_next_wrap (buf->xtext, ent, str, win_width, indent);
		if (taken < RECORD_WRAPS)
			ent->wrap_offset[taken] = (str + len) - ent->str;
		indent = buf->indent;
		taken++;
		str += len;
	}
	while (str < ent->str + ent->str_len);

	return taken;
}

/* Calculate number of actual lines (with wraps), to set adj->lower. *
 * This should only be called when the window resizes.               */

static void
gtk_xtext_calc_lines (xtext_buffer *buf, int fire_signal)
{
	textentry *ent;
	int width;
	int height;
	int lines;

	width = gdk_window_get_width (gtk_widget_get_window (GTK_WIDGET(buf->xtext)));
	height = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(buf->xtext)));
	width -= MARGIN;

	if (width < 30 || height < buf->xtext->fontsize || width < buf->indent + 30)
		return;

	lines = 0;
	ent = buf->text_first;
	while (ent)
	{
		ent->lines_taken = gtk_xtext_lines_taken (buf, ent);
		lines += ent->lines_taken;
		ent = ent->next;
	}

	buf->pagetop_ent = NULL;
	buf->num_lines = lines;
	gtk_xtext_adjustment_set (buf, fire_signal);
}

/* find the n-th line in the linked list, this includes wrap calculations */

static textentry *
gtk_xtext_nth (GtkXText *xtext, int line, int *subline)
{
	int lines = 0;
	textentry *ent;

	ent = xtext->buffer->text_first;

	/* -- optimization -- try to make a short-cut using the pagetop ent */
	if (xtext->buffer->pagetop_ent)
	{
		if (line == xtext->buffer->pagetop_line)
		{
			*subline = xtext->buffer->pagetop_subline;
			return xtext->buffer->pagetop_ent;
		}
		if (line > xtext->buffer->pagetop_line)
		{
			/* lets start from the pagetop instead of the absolute beginning */
			ent = xtext->buffer->pagetop_ent;
			lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
		}
		else if (line > xtext->buffer->pagetop_line - line)
		{
			/* move backwards from pagetop */
			ent = xtext->buffer->pagetop_ent;
			lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
			while (1)
			{
				if (lines <= line)
				{
					*subline = line - lines;
					return ent;
				}
				ent = ent->prev;
				if (!ent)
					break;
				lines -= ent->lines_taken;
			}
			return 0;
		}
	}
	/* -- end of optimization -- */

	while (ent)
	{
		lines += ent->lines_taken;
		if (lines > line)
		{
			*subline = ent->lines_taken - (lines - line);
			return ent;
		}
		ent = ent->next;
	}
	return 0;
}

/* render enta (or an inclusive range enta->entb) */

static int
gtk_xtext_render_ents (GtkXText * xtext, textentry * enta, textentry * entb)
{
	textentry *ent, *orig_ent, *tmp_ent;
	int line;
	int lines_max;
	int width;
	int height;
	int subline;
	int drawing = FALSE;

	if (xtext->buffer->indent < MARGIN)
		xtext->buffer->indent = MARGIN;	  /* 2 pixels is our left margin */

	width = gdk_window_get_width   (gtk_widget_get_window (GTK_WIDGET(xtext)));
	height = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(xtext)));
	width -= MARGIN;

	if (width < 32 || height < xtext->fontsize || width < xtext->buffer->indent + 30)
		return 0;

	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;
	line = 0;
	orig_ent = xtext->buffer->pagetop_ent;
	subline = xtext->buffer->pagetop_subline;

	/* used before a complete page is in buffer */
	if (orig_ent == NULL)
		orig_ent = xtext->buffer->text_first;

	/* check if enta is before the start of this page */
	if (entb)
	{
		tmp_ent = orig_ent;
		while (tmp_ent)
		{
			if (tmp_ent == enta)
				break;
			if (tmp_ent == entb)
			{
				drawing = TRUE;
				break;
			}
			tmp_ent = tmp_ent->next;
		}
	}

	ent = orig_ent;
	while (ent)
	{
		if (entb && ent == enta)
			drawing = TRUE;

		if (drawing || ent == entb || ent == enta)
		{
			gtk_xtext_reset (xtext, FALSE, TRUE);
			line += gtk_xtext_render_line (xtext, ent, line, lines_max,
													 subline, width);
			subline = 0;
			xtext->jump_in_offset = 0;	/* jump_in_offset only for the 1st */
		} else
		{
			if (ent == orig_ent)
			{
				line -= subline;
				subline = 0;
			}
			line += ent->lines_taken;
		}

		if (ent == entb)
			break;

		if (line >= lines_max)
			break;

		ent = ent->next;
	}

	/* space below last line */
	return (xtext->fontsize * line) - xtext->pixel_offset;
}

/* render a whole page/window, starting from 'startline' */

static void
gtk_xtext_render_page (GtkXText * xtext)
{
	textentry *ent;
	int line;
	int lines_max;
	int width;
	int height;
	int subline;
	int startline = gtk_adjustment_get_value (xtext->adj);

	if(!gtk_widget_get_realized (GTK_WIDGET (xtext)))
	  return;

	if (xtext->buffer->indent < MARGIN)
		xtext->buffer->indent = MARGIN;	  /* 2 pixels is our left margin */

	width = gdk_window_get_width   (gtk_widget_get_window (GTK_WIDGET(xtext)));
	height = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(xtext)));

	if (width < 34 || height < xtext->fontsize || width < xtext->buffer->indent + 32)
		return;

	xtext->pixel_offset = (gtk_adjustment_get_value (xtext->adj) - startline) * xtext->fontsize;

	subline = line = 0;
	ent = xtext->buffer->text_first;

	if (startline > 0)
		ent = gtk_xtext_nth (xtext, startline, &subline);

	xtext->buffer->pagetop_ent = ent;
	xtext->buffer->pagetop_subline = subline;
	xtext->buffer->pagetop_line = startline;

	xtext->buffer->grid_dirty = FALSE;
	width -= MARGIN;
	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;

	while (ent)
	{
		gtk_xtext_reset (xtext, FALSE, TRUE);
		line += gtk_xtext_render_line (xtext, ent, line, lines_max,
												 subline, width);
		subline = 0;

		if (line >= lines_max)
			break;

		ent = ent->next;
	}

	line = (xtext->fontsize * line) - xtext->pixel_offset;
	/* fill any space below the last line with our background GC */
	xtext_draw_bg (xtext, 0, line, width + MARGIN, height - line);

	/* draw the separator line */
	gtk_xtext_draw_sep (xtext, -1);
}

void
gtk_xtext_refresh (GtkXText * xtext, int do_trans)
{
	if (gtk_widget_get_realized (GTK_WIDGET (xtext)))
	{
		gtk_xtext_render_page (xtext);
	}
}

/* remove the topline from the list */

static void
gtk_xtext_remove_top (xtext_buffer *buffer)
{
	textentry *ent;

	ent = buffer->text_first;
	if (!ent)
		return;
	buffer->num_lines -= ent->lines_taken;
	buffer->pagetop_line -= ent->lines_taken;
	buffer->last_pixel_pos -= (ent->lines_taken * buffer->xtext->fontsize);
	buffer->text_first = ent->next;
	buffer->text_first->prev = NULL;

	buffer->old_value -= ent->lines_taken;
	if (buffer->xtext->buffer == buffer)	/* is it the current buffer? */
	{
		gtk_adjustment_set_value (buffer->xtext->adj, ent->lines_taken);
		buffer->xtext->select_start_adj -= ent->lines_taken;
	}

	if (ent == buffer->pagetop_ent)
		buffer->pagetop_ent = NULL;

	if (ent == buffer->last_ent_start)
		buffer->last_ent_start = ent->next;

	if (ent == buffer->last_ent_end)
	{
		buffer->last_ent_start = NULL;
		buffer->last_ent_end = NULL;
	}

	if (buffer->marker_pos == ent) buffer->marker_pos = NULL;

	free (ent);
}

void
gtk_xtext_clear (xtext_buffer *buf)
{
	textentry *next;

	if (buf->xtext->auto_indent)
		buf->indent = MARGIN;
	buf->scrollbar_down = TRUE;
	buf->last_ent_start = NULL;
	buf->last_ent_end = NULL;
	buf->marker_pos = NULL;
	buf->laststamp[0] = 0;

	while (buf->text_first)
	{
		next = buf->text_first->next;
		free (buf->text_first);
		buf->text_first = next;
	}
	buf->text_last = NULL;

	if (buf->xtext->buffer == buf)
	{
		gtk_xtext_calc_lines (buf, TRUE);
		gtk_xtext_refresh (buf->xtext, 0);
	} else
	{
		gtk_xtext_calc_lines (buf, FALSE);
	}
}

static gboolean
gtk_xtext_check_ent_visibility (GtkXText * xtext, textentry *find_ent, int add)
{
	textentry *ent;
	int lines_max;
	int line = 0;
	int height;

	height = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(xtext)));

	lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + add;
	ent = xtext->buffer->pagetop_ent;

	while (ent && line < lines_max)
	{
		if (find_ent == ent)
			return TRUE;
		line += ent->lines_taken;
		ent = ent->next;
	}

	return FALSE;
}

void
gtk_xtext_check_marker_visibility (GtkXText * xtext)
{
	if (gtk_xtext_check_ent_visibility (xtext, xtext->buffer->marker_pos, 1))
		xtext->buffer->marker_seen = TRUE;
}

textentry *
gtk_xtext_search (GtkXText * xtext, const gchar *text, textentry *start, gboolean case_match, gboolean backward)
{
	textentry *ent, *fent;
	int line;
	gchar *str, *nee, *hay;	/* needle in haystack */

	gtk_xtext_selection_clear_full (xtext->buffer);
	xtext->buffer->last_ent_start = NULL;
	xtext->buffer->last_ent_end = NULL;

	/* set up text comparand for Case Match or Ignore */
	if (case_match)
		nee = g_strdup (text);
	else
		nee = g_utf8_casefold (text, strlen (text));

	/* Validate that start gives a currently valid ent pointer */
	ent = xtext->buffer->text_first;
	while (ent)
	{
		if (ent == start)
			break;
		ent = ent->next;
	}
	if (!ent)
		start = NULL;

	/* Choose first ent to look at */
	if (start)
		ent = backward? start->prev: start->next;
	else
		ent = backward? xtext->buffer->text_last: xtext->buffer->text_first;

	/* Search from there to one end or the other until found */
	while (ent)
	{
		/* If Case Ignore, fold before & free after calling strstr */
		if (case_match)
			hay = g_strdup ((char *) ent->str);
		else
			hay = g_utf8_casefold ((char *) ent->str, strlen ((char *) ent->str));
		/* Try to find the needle in this haystack */
		str = g_strstr_len (hay, strlen (hay), nee);
		g_free (hay);
		if (str)
			break;
		ent = backward? ent->prev: ent->next;
	}
	fent = ent;

	/* Save distance to start, end of found string */
	if (ent)
	{
		ent->mark_start = str - hay;
		ent->mark_end = ent->mark_start + strlen (nee);

		/* is the match visible? Might need to scroll */
		if (!gtk_xtext_check_ent_visibility (xtext, ent, 0))
		{
			ent = xtext->buffer->text_first;
			line = 0;
			while (ent)
			{
				line += ent->lines_taken;
				ent = ent->next;
				if (ent == fent)
					break;
			}
			while (line > gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj))
				line--;

			gtk_adjustment_set_value (xtext->adj, line);
			xtext->buffer->scrollbar_down = FALSE;
			gtk_adjustment_changed (xtext->adj);
		}
	}

	g_free (nee);
	gtk_widget_queue_draw (GTK_WIDGET (xtext));

	return fent;
}

static int
gtk_xtext_render_page_timeout (GtkXText * xtext)
{
	GtkAdjustment *adj = xtext->adj;

	xtext->add_io_tag = 0;

	/* less than a complete page? */
	if (xtext->buffer->num_lines <= gtk_adjustment_get_page_size (adj))
	{
		xtext->buffer->old_value = 0;
		gtk_adjustment_set_value (adj, 0);
		gtk_xtext_render_page (xtext);
	} else if (xtext->buffer->scrollbar_down)
	{
		g_signal_handler_block (xtext->adj, xtext->vc_signal_tag);
		gtk_xtext_adjustment_set (xtext->buffer, FALSE);
		gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj));
		g_signal_handler_unblock (xtext->adj, xtext->vc_signal_tag);
		xtext->buffer->old_value = gtk_adjustment_get_value (adj);
		gtk_xtext_render_page (xtext);
	} else
	{
		gtk_xtext_adjustment_set (xtext->buffer, TRUE);
		if (xtext->indent_changed)
		{
			xtext->indent_changed = FALSE;
			gtk_xtext_render_page (xtext);
		}
	}

	return 0;
}

/* append a textentry to our linked list */

static void
gtk_xtext_append_entry (xtext_buffer *buf, textentry * ent, time_t stamp)
{
	int mb;
	int i;

	/* we don't like tabs */
	i = 0;
	while (i < ent->str_len)
	{
		if (ent->str[i] == '\t')
			ent->str[i] = ' ';
		i++;
	}

	ent->stamp = stamp;
	if (stamp == 0)
		ent->stamp = time (0);
	ent->str_width = gtk_xtext_text_width (buf->xtext, ent->str, ent->str_len, &mb);
	ent->mb = FALSE;
	if (mb)
		ent->mb = TRUE;
	ent->mark_start = -1;
	ent->mark_end = -1;
	ent->next = NULL;

	if (ent->indent < MARGIN)
		ent->indent = MARGIN;	  /* 2 pixels is the left margin */

	/* append to our linked list */
	if (buf->text_last)
		buf->text_last->next = ent;
	else
		buf->text_first = ent;
	ent->prev = buf->text_last;
	buf->text_last = ent;

	ent->lines_taken = gtk_xtext_lines_taken (buf, ent);
	buf->num_lines += ent->lines_taken;

	if (buf->reset_marker_pos || 
		((buf->marker_pos == NULL || buf->marker_seen) && (buf->xtext->buffer != buf || 
		!gtk_window_has_toplevel_focus (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (buf->xtext)))))))
	{
		buf->marker_pos = ent;
		buf->marker_seen = FALSE;
		buf->reset_marker_pos = FALSE;
	}

	if (buf->xtext->max_lines > 2 && buf->xtext->max_lines < buf->num_lines)
	{
		gtk_xtext_remove_top (buf);
	}

	if (buf->xtext->buffer == buf)
	{
		if (!buf->xtext->add_io_tag)
		{
			/* remove scrolling events */
			if (buf->xtext->io_tag)
			{
				g_source_remove (buf->xtext->io_tag);
				buf->xtext->io_tag = 0;
			}
			buf->xtext->add_io_tag = g_timeout_add (REFRESH_TIMEOUT * 2,
															(GSourceFunc)
															gtk_xtext_render_page_timeout,
															buf->xtext);
		}
	} else if (buf->scrollbar_down)
	{
		buf->old_value = buf->num_lines - gtk_adjustment_get_page_size (buf->xtext->adj);
		if (buf->old_value < 0)
			buf->old_value = 0;
	}
}

/* the main two public functions */

void
gtk_xtext_append_indent (xtext_buffer *buf,
								 unsigned char *left_text, int left_len,
								 unsigned char *right_text, int right_len,
								 time_t stamp)
{
	textentry *ent;
	unsigned char *str;
	int space;
	int tempindent;
	int left_width;

	if (left_len == -1)
		left_len = strlen ((char *) left_text);

	if (right_len == -1)
		right_len = strlen ((char *) right_text);

	if (right_len >= sizeof (buf->xtext->scratch_buffer))
		right_len = sizeof (buf->xtext->scratch_buffer) - 1;

	if (right_text[right_len-1] == '\n')
		right_len--;

	ent = malloc (left_len + right_len + 2 + sizeof (textentry));
	str = (unsigned char *) ent + sizeof (textentry);

	memcpy (str, left_text, left_len);
	str[left_len] = ' ';
	memcpy (str + left_len + 1, right_text, right_len);
	str[left_len + 1 + right_len] = 0;

	left_width = gtk_xtext_text_width (buf->xtext, left_text, left_len, NULL);

	ent->left_len = left_len;
	ent->str = str;
	ent->str_len = left_len + 1 + right_len;
	ent->indent = (buf->indent - left_width) - buf->xtext->space_width;

	if (buf->time_stamp)
		space = buf->xtext->stamp_width;
	else
		space = 0;

	/* do we need to auto adjust the separator position? */
	if (buf->xtext->auto_indent && ent->indent < MARGIN + space)
	{
		tempindent = MARGIN + space + buf->xtext->space_width + left_width;

		if (tempindent > buf->indent)
			buf->indent = tempindent;

		if (buf->indent > buf->xtext->max_auto_indent)
			buf->indent = buf->xtext->max_auto_indent;

		gtk_xtext_fix_indent (buf);
		gtk_xtext_recalc_widths (buf, FALSE);

		ent->indent = (buf->indent - left_width) - buf->xtext->space_width;
		buf->xtext->indent_changed = TRUE;
	}

	gtk_xtext_append_entry (buf, ent, stamp);
}

void
gtk_xtext_append (xtext_buffer *buf, unsigned char *text, int len)
{
	textentry *ent;

	if (len == -1)
		len = strlen ((char *) text);

	if (text[len-1] == '\n')
		len--;

	if (len >= sizeof (buf->xtext->scratch_buffer))
		len = sizeof (buf->xtext->scratch_buffer) - 1;

	ent = malloc (len + 1 + sizeof (textentry));
	ent->str = (unsigned char *) ent + sizeof (textentry);
	ent->str_len = len;
	if (len)
		memcpy (ent->str, text, len);
	ent->str[len] = 0;
	ent->indent = 0;
	ent->left_len = -1;

	gtk_xtext_append_entry (buf, ent, 0);
}

gboolean
gtk_xtext_is_empty (xtext_buffer *buf)
{
	return buf->text_first == NULL;
}

int
gtk_xtext_lastlog (xtext_buffer *out, xtext_buffer *search_area,
						 int (*cmp_func) (char *, void *), void *userdata)
{
	textentry *ent = search_area->text_first;
	int matches = 0;

	while (ent)
	{
		if (cmp_func ((char *) ent->str, userdata))
		{
			matches++;
			/* copy the text over */
			if (search_area->xtext->auto_indent)
				gtk_xtext_append_indent (out, ent->str, ent->left_len,
												 ent->str + ent->left_len + 1,
												 ent->str_len - ent->left_len - 1, 0);
			else
				gtk_xtext_append (out, ent->str, ent->str_len);
			/* copy the timestamp over */
			out->text_last->stamp = ent->stamp;
		}
		ent = ent->next;
	}

	return matches;
}

void
gtk_xtext_foreach (xtext_buffer *buf, GtkXTextForeach func, void *data)
{
	textentry *ent = buf->text_first;

	while (ent)
	{
		(*func) (buf->xtext, ent->str, data);
		ent = ent->next;
	}
}

void
gtk_xtext_set_error_function (GtkXText *xtext, void (*error_function) (int))
{
	xtext->error_function = error_function;
}

void
gtk_xtext_set_indent (GtkXText *xtext, gboolean indent)
{
	xtext->auto_indent = indent;
}

void
gtk_xtext_set_max_indent (GtkXText *xtext, int max_auto_indent)
{
	xtext->max_auto_indent = max_auto_indent;
}

void
gtk_xtext_set_max_lines (GtkXText *xtext, int max_lines)
{
	xtext->max_lines = max_lines;
}

void
gtk_xtext_set_show_marker (GtkXText *xtext, gboolean show_marker)
{
	xtext->marker = show_marker;
}

void
gtk_xtext_set_show_separator (GtkXText *xtext, gboolean show_separator)
{
	xtext->separator = show_separator;
}

void
gtk_xtext_set_thin_separator (GtkXText *xtext, gboolean thin_separator)
{
	xtext->thinline = thin_separator;
}

void
gtk_xtext_set_time_stamp (xtext_buffer *buf, gboolean time_stamp)
{
	buf->time_stamp = time_stamp;
}

void
gtk_xtext_set_urlcheck_function (GtkXText *xtext, int (*urlcheck_function) (GtkWidget *, char *, int))
{
	xtext->urlcheck_function = urlcheck_function;
}

void
gtk_xtext_set_wordwrap (GtkXText *xtext, gboolean wordwrap)
{
	xtext->wordwrap = wordwrap;
}

void
gtk_xtext_reset_marker_pos (GtkXText *xtext)
{
	xtext->buffer->marker_pos = NULL;
	gtk_xtext_render_page (xtext);
	xtext->buffer->reset_marker_pos = TRUE;
}

void
gtk_xtext_buffer_show (GtkXText *xtext, xtext_buffer *buf, int render)
{
	int w, h;

	buf->xtext = xtext;

	if (xtext->buffer == buf)
		return;

/*printf("text_buffer_show: xtext=%p buffer=%p\n", xtext, buf);*/

	if (xtext->add_io_tag)
	{
		g_source_remove (xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->io_tag)
	{
		g_source_remove (xtext->io_tag);
		xtext->io_tag = 0;
	}

	if (!gtk_widget_get_realized (GTK_WIDGET (xtext)))
		gtk_widget_realize (GTK_WIDGET (xtext));
	w = gdk_window_get_width  (gtk_widget_get_window (GTK_WIDGET(xtext)));
	h = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET(xtext)));

	/* after a font change */
	if (buf->needs_recalc)
	{
		buf->needs_recalc = FALSE;
		gtk_xtext_recalc_widths (buf, TRUE);
	}

	/* now change to the new buffer */
	xtext->buffer = buf;
	gtk_adjustment_set_value (xtext->adj, buf->old_value);
	gtk_adjustment_set_upper (xtext->adj, buf->num_lines? buf->num_lines: 1);
	/* sanity check */
	if (gtk_adjustment_get_value (xtext->adj) > gtk_adjustment_get_upper (xtext->adj) - 
			gtk_adjustment_get_page_size (xtext->adj))
	{
		int val = gtk_adjustment_get_upper (xtext->adj) - gtk_adjustment_get_page_size (xtext->adj);
		gtk_adjustment_set_value (xtext->adj, (val > 0)? val: 0);
	}

	if (render)
	{
		/* did the window change size since this buffer was last shown? */
		if (buf->window_width != w)
		{
			buf->window_width = w;
			gtk_xtext_calc_lines (buf, FALSE);
			if (buf->scrollbar_down)
				gtk_adjustment_set_value (xtext->adj, gtk_adjustment_get_upper (xtext->adj) -
					gtk_adjustment_get_page_size (xtext->adj));
		} else if (buf->window_height != h)
		{
			buf->window_height = h;
			buf->pagetop_ent = NULL;
			gtk_xtext_adjustment_set (buf, FALSE);
		}

		gtk_xtext_render_page (xtext);
		gtk_adjustment_changed (xtext->adj);
	}
}

xtext_buffer *
gtk_xtext_buffer_new (GtkXText *xtext)
{
	xtext_buffer *buf;

	buf = malloc (sizeof (xtext_buffer));
	memset (buf, 0, sizeof (xtext_buffer));
	buf->old_value = -1;
	buf->xtext = xtext;
	buf->scrollbar_down = TRUE;
	buf->indent = xtext->space_width * 2;

	return buf;
}

void
gtk_xtext_buffer_free (xtext_buffer *buf)
{
	textentry *ent, *next;

	if (buf->xtext->buffer == buf)
		buf->xtext->buffer = buf->xtext->orig_buffer;

	if (buf->xtext->selection_buffer == buf)
		buf->xtext->selection_buffer = NULL;

	ent = buf->text_first;
	while (ent)
	{
		next = ent->next;
		free (ent);
		ent = next;
	}

	free (buf);
}


void
gtk_xtext_copy_selection (GtkXText *xtext)
{
	char *str;
	int len;

	str = gtk_xtext_selection_get_text (xtext, &len);
	if (str) {
		GtkClipboard *clipboard = gtk_widget_get_clipboard (GTK_WIDGET (xtext), GDK_SELECTION_CLIPBOARD);
		gtk_clipboard_set_text (clipboard, str, len);
		g_free (str);
	}
}

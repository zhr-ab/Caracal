#include "css_style.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "css_style";

/* ---- helpers ---- */

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int parse_px(const char *val)
{
    /* Parse "Npx" or just "N" */
    int v = atoi(val);
    return v;
}

static int parse_percent_val(const char *val, bool *is_percent)
{
    *is_percent = false;
    const char *p = val;
    while (*p && *p != '%') p++;
    if (*p == '%') *is_percent = true;
    return atoi(val);
}

/* ---- public API ---- */

css_style_t css_style_default(void)
{
    css_style_t s = {0};
    s.display = CSS_DISPLAY_BLOCK;
    s.flex_direction = CSS_FLEX_DIR_ROW;
    s.justify_content = CSS_JUSTIFY_START;
    s.align_items = CSS_ALIGN_STRETCH;
    s.flex_wrap = CSS_FLEX_NOWRAP;
    s.grid_col_count = 0;
    s.grid_row_count = 0;
    s.gap = 0;
    s.padding = 0;
    s.margin = 0;
    s.width = -1;
    s.height = -1;
    s.min_width = -1;
    s.min_height = -1;
    s.flex_grow = 0;
    s.flex_shrink = 1;
    s.flex_basis = -1;
    s.font_size = -1;
    s.text_color = -1;
    s.has_style = false;
    return s;
}

/* Parse a CSS grid-template-columns/rows value like "1fr 1fr 1fr" or "100px 50% auto" */
static int parse_track_list(const char *val, int *sizes, bool *is_percent, int max_tracks)
{
    int count = 0;
    const char *p = val;

    while (*p && count < max_tracks) {
        skip_ws(&p);
        if (!*p) break;

        if (starts_with(p, "auto")) {
            sizes[count] = -1;
            is_percent[count] = false;
            count++;
            p += 4;
        } else if (starts_with(p, "1fr") || starts_with(p, "fr")) {
            /* Treat 1fr as -2 (flexible track) */
            sizes[count] = -2;
            is_percent[count] = false;
            count++;
            while (*p && *p != ' ' && *p != ',') p++;
        } else {
            bool pct = false;
            int v = parse_percent_val(p, &pct);
            sizes[count] = v;
            is_percent[count] = pct;
            count++;
            while (*p && *p != ' ' && *p != ',') p++;
        }

        skip_ws(&p);
        if (*p == ',') p++;
    }
    return count;
}

css_style_t css_style_parse(const char *style_str)
{
    css_style_t s = css_style_default();
    if (!style_str || !*style_str) return s;

    /* Make a mutable copy to tokenize */
    char *buf = strdup(style_str);
    if (!buf) return s;

    char *saveptr = NULL;
    char *decl = strtok_r(buf, ";", &saveptr);

    while (decl) {
        /* Trim leading whitespace */
        while (*decl == ' ' || *decl == '\t') decl++;

        /* Find colon separator */
        char *colon = strchr(decl, ':');
        if (!colon) { decl = strtok_r(NULL, ";", &saveptr); continue; }

        *colon = '\0';
        char *prop = decl;
        char *val = colon + 1;

        /* Trim prop trailing ws */
        char *pe = prop + strlen(prop) - 1;
        while (pe > prop && (*pe == ' ' || *pe == '\t')) *pe-- = '\0';

        /* Trim val leading ws */
        while (*val == ' ' || *val == '\t') val++;

        /* Trim val trailing ws */
        char *ve = val + strlen(val) - 1;
        while (ve > val && (*ve == ' ' || *ve == '\t' || *ve == '\r' || *ve == '\n')) *ve-- = '\0';

        s.has_style = true;

        /* ---- Property matching ---- */

        if (strcmp(prop, "display") == 0) {
            if (strcmp(val, "flex") == 0)          s.display = CSS_DISPLAY_FLEX;
            else if (strcmp(val, "grid") == 0)      s.display = CSS_DISPLAY_GRID;
            else if (strcmp(val, "inline") == 0)    s.display = CSS_DISPLAY_INLINE;
            else if (strcmp(val, "none") == 0)      s.display = CSS_DISPLAY_NONE;
            else                                    s.display = CSS_DISPLAY_BLOCK;
        }
        else if (strcmp(prop, "flex-direction") == 0) {
            if (strcmp(val, "column") == 0)     s.flex_direction = CSS_FLEX_DIR_COLUMN;
            else                                s.flex_direction = CSS_FLEX_DIR_ROW;
        }
        else if (strcmp(prop, "justify-content") == 0) {
            if (strcmp(val, "center") == 0)             s.justify_content = CSS_JUSTIFY_CENTER;
            else if (strcmp(val, "flex-end") == 0)      s.justify_content = CSS_JUSTIFY_END;
            else if (strcmp(val, "space-between") == 0) s.justify_content = CSS_JUSTIFY_SPACE_BETWEEN;
            else                                        s.justify_content = CSS_JUSTIFY_START;
        }
        else if (strcmp(prop, "align-items") == 0) {
            if (strcmp(val, "center") == 0)         s.align_items = CSS_ALIGN_CENTER;
            else if (strcmp(val, "flex-end") == 0)  s.align_items = CSS_ALIGN_END;
            else if (strcmp(val, "stretch") == 0)   s.align_items = CSS_ALIGN_STRETCH;
            else                                    s.align_items = CSS_ALIGN_START;
        }
        else if (strcmp(prop, "flex-wrap") == 0) {
            if (strcmp(val, "wrap") == 0)   s.flex_wrap = CSS_FLEX_WRAP;
            else                            s.flex_wrap = CSS_FLEX_NOWRAP;
        }
        else if (strcmp(prop, "gap") == 0) {
            s.gap = parse_px(val);
        }
        else if (strcmp(prop, "padding") == 0) {
            s.padding = parse_px(val);
        }
        else if (strcmp(prop, "margin") == 0) {
            s.margin = parse_px(val);
        }
        else if (strcmp(prop, "width") == 0) {
            if (strcmp(val, "auto") != 0) s.width = parse_px(val);
        }
        else if (strcmp(prop, "height") == 0) {
            if (strcmp(val, "auto") != 0) s.height = parse_px(val);
        }
        else if (strcmp(prop, "min-width") == 0) {
            s.min_width = parse_px(val);
        }
        else if (strcmp(prop, "min-height") == 0) {
            s.min_height = parse_px(val);
        }
        else if (strcmp(prop, "flex-grow") == 0) {
            s.flex_grow = atoi(val);
        }
        else if (strcmp(prop, "flex-shrink") == 0) {
            s.flex_shrink = atoi(val);
        }
        else if (strcmp(prop, "flex-basis") == 0) {
            if (strcmp(val, "auto") == 0) s.flex_basis = -1;
            else s.flex_basis = parse_px(val);
        }
        else if (strcmp(prop, "grid-template-columns") == 0) {
            s.grid_col_count = parse_track_list(val, s.grid_col_sizes,
                                                 s.grid_col_is_percent,
                                                 CSS_GRID_MAX_TRACKS);
        }
        else if (strcmp(prop, "grid-template-rows") == 0) {
            s.grid_row_count = parse_track_list(val, s.grid_row_sizes,
                                                 s.grid_row_is_percent,
                                                 CSS_GRID_MAX_TRACKS);
        }
        else if (strcmp(prop, "font-size") == 0) {
            s.font_size = parse_px(val);
        }
        else if (strcmp(prop, "color") == 0) {
            if (val[0] == '#') {
                s.text_color = (int)strtol(val + 1, NULL, 16);
            }
        }

        decl = strtok_r(NULL, ";", &saveptr);
    }

    free(buf);
    ESP_LOGD(TAG, "Parsed style: display=%s gap=%d padding=%d",
             css_display_str(s.display), s.gap, s.padding);
    return s;
}

const char *css_display_str(css_display_t d)
{
    switch (d) {
    case CSS_DISPLAY_BLOCK:   return "block";
    case CSS_DISPLAY_FLEX:    return "flex";
    case CSS_DISPLAY_GRID:    return "grid";
    case CSS_DISPLAY_INLINE:  return "inline";
    case CSS_DISPLAY_NONE:    return "none";
    default:                  return "?";
    }
}

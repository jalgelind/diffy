#include "config.hpp"

#include <highlight/highlight_group.hpp>
#include <highlight/highlight_palette.hpp>
#include <highlight/language.hpp>
#include <output/column_view.hpp>
#include <util/color.hpp>

#include <fmt/format.h>
#include <sago/platform_folders.h>
#include <config_parser/config_parser.hpp>
#include <config_parser/config_serializer.hpp>
#include <config_parser/config_parser_utils.hpp>

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

static std::string config_doc_theme = R"foo(# Theme configuration
# 
# Customize colors using the `color_map` table for global mappings
# or changing the color style of each specific theme item.
#
# You can re-map these colors in `color_map` below. Supported
# values are the palette names are as follow:
#
# RGB hex colors:
#   '#RGB' and '#RRGGBB'. I.e '#F00' or '#FF0000' for bright red.
#
# 256 color palette (see https://www.ditig.com/256-colors-cheat-sheet):
#   'P<palette-index>', I.e 'P196' for the color known as "Red1"
#
# 16 color palette SGR colors:
#   black, red, green, yellow, blue, magenta, cyan, light_gray,
#   dark_gray, light_red, light_green, light_yellow, light_blue,
#   light_magenta, light_cyan, white
#
# Available attributes:
#   'bold', 'dim', 'italic', 'underline',
#   'blink', 'inverse', 'hidden', 'strikethrough'
#
)foo";

static std::string color_map_comment = R"foo(# Custom color map with aliases
# E.g:
#   red = '#FF1111'
#   background = 'black')foo";

static std::string config_doc_general = R"foo(# General configuration for ´diffy´
#
# Configure default options. These can be overriden with command-line arguments.
#
# To use another theme, update `theme` to point to another theme file; i.e:
#   theme = 'custom_theme'  # load custom_theme.conf
#
# Bundled themes (created in the config directory on first run):
#   theme_default, theme_paper, theme_ink, theme_studio_dark, theme_studio_light,
#   theme_ember_dark, theme_ember_light, theme_catppuccin_mocha,
#   theme_catppuccin_latte, theme_tokyo_night, theme_tokyo_night_day
#
# Syntax highlighting grammars are loaded from 'grammars/' next to the diffy
# executable, or dropped into '<this directory>/grammars/' (a tree-sitter
# grammar library plus a <name>.scm highlights query). Map file extensions or
# exact filenames to a grammar with:
#   [highlight]
#   extensions = { cpp = ['.tpp', '.ixx'], zig = '.zig' }
#
)foo";

// Bundled themes written alongside the default theme on first-run setup. Each is
// fully self-contained — it sets both a foreground and a background on every
// element (and a base `style.background`) — so it renders identically regardless
// of the terminal's default colors, unlike the foreground-only default theme.
// Stored as raw .conf text because hex colors cannot round-trip through
// TermStyle::to_value() (which only knows palette names); the load path parses
// hex fine.
// Each bundled theme declares a human-facing label via `[meta] name`, resolved by
// config_theme_display_name(). The moved-block accent is theme-driven via
// `style.moved_line` (GAP-9): its fg recolours a relocated line's number, kept
// clearly distinct from that theme's keyword and its add-green / delete-red.

static std::string theme_paper_conf =
    R"foo([meta]
name = 'Paper'

# Paper — a crisp, high-contrast light theme (the classic light-editor look).
# Self-contained: every element sets fg + bg, so it renders identically on any
# terminal. On a light theme, changed tokens are highlighted with a stronger
# *background* (the foreground stays dark) to avoid muddy foreground blends.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#1f2328', bg = '#ffffff', attr = [] }
header              = { fg = '#0969da', bg = '#ddf4ff', attr = ['bold', 'underline'] }
delete_line         = { fg = '#1f2328', bg = '#ffebe9', attr = [] }
delete_token        = { fg = '#1f2328', bg = '#ffc4c2', attr = ['bold'] }
delete_line_number  = { fg = '#cf222e', bg = '#ffd7d5', attr = [] }
insert_line         = { fg = '#1f2328', bg = '#e6ffec', attr = [] }
insert_token        = { fg = '#1f2328', bg = '#abf2bc', attr = ['bold'] }
insert_line_number  = { fg = '#116329', bg = '#ccffd8', attr = [] }
common_line         = { fg = '#1f2328', bg = '#ffffff', attr = [] }
common_line_number  = { fg = '#6e7781', bg = '#ffffff', attr = [] }
frame               = { fg = '#d0d7de', bg = '#ffffff', attr = [] }
empty_cell          = { fg = '#6e7781', bg = '#ffffff', attr = [] }
moved_line          = { fg = '#7c3aed', bg = 'none', attr = [] }

# Syntax palette (tree-sitter groups). Omitted groups fall back to the built-in
# default for the theme's light/dark.
[syntax]
comment          = '#6e7781'
keyword          = '#cf222e'
operator         = '#0550ae'
punctuation      = '#1f2328'
string           = '#0a3069'
escape           = '#0a3069'
number           = '#0550ae'
boolean          = '#0550ae'
constant         = '#0550ae'
constant_builtin = '#0550ae'
function         = '#8250df'
method           = '#8250df'
constructor      = '#8250df'
type             = '#8250df'
type_builtin     = '#cf222e'
variable         = '#953800'
parameter        = '#1f2328'
property         = '#953800'
namespace        = '#8250df'
tag              = '#116329'
attribute        = '#0550ae'
)foo";

static std::string theme_ink_conf =
    R"foo([meta]
name = 'Ink'

# Ink — a cool, high-contrast dark theme (deep blue-black, blue accent).
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#e6edf3', bg = '#0d1117', attr = [] }
header              = { fg = '#58a6ff', bg = '#161b22', attr = ['bold', 'underline'] }
delete_line         = { fg = '#e6edf3', bg = '#341a1f', attr = [] }
delete_token        = { fg = '#f85149', bg = '#6a2b30', attr = ['bold'] }
delete_line_number  = { fg = '#f85149', bg = '#47232a', attr = [] }
insert_line         = { fg = '#e6edf3', bg = '#12261e', attr = [] }
insert_token        = { fg = '#3fb950', bg = '#1f5c37', attr = ['bold'] }
insert_line_number  = { fg = '#3fb950', bg = '#16351f', attr = [] }
common_line         = { fg = '#e6edf3', bg = '#0d1117', attr = [] }
common_line_number  = { fg = '#6e7681', bg = '#0d1117', attr = [] }
frame               = { fg = '#30363d', bg = '#0d1117', attr = [] }
empty_cell          = { fg = '#6e7681', bg = '#0d1117', attr = [] }
moved_line          = { fg = '#a371f7', bg = 'none', attr = [] }

[syntax]
comment          = '#8b949e'
keyword          = '#ff7b72'
operator         = '#ff7b72'
punctuation      = '#c9d1d9'
string           = '#a5d6ff'
escape           = '#a5d6ff'
number           = '#79c0ff'
boolean          = '#79c0ff'
constant         = '#79c0ff'
constant_builtin = '#79c0ff'
function         = '#d2a8ff'
method           = '#d2a8ff'
constructor      = '#d2a8ff'
type             = '#ffa657'
type_builtin     = '#79c0ff'
variable         = '#ffa657'
parameter        = '#c9d1d9'
property         = '#79c0ff'
namespace        = '#ffa657'
tag              = '#7ee787'
attribute        = '#79c0ff'
)foo";

static std::string theme_studio_dark_conf =
    R"foo([meta]
name = 'Studio Dark'

# Studio Dark — cool blue-black surfaces with a warm-orange accent (the hero
# dark). Self-contained: every element sets fg + bg. Syntax deliberately avoids
# the violet band so the moved accent stays unambiguous.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#f2f4f9', bg = '#121418', attr = [] }
header              = { fg = '#ff8b3d', bg = '#1f2531', attr = ['bold', 'underline'] }
delete_line         = { fg = '#f2f4f9', bg = '#3a1d22', attr = [] }
delete_token        = { fg = '#ff6b6b', bg = '#5a2830', attr = ['bold'] }
delete_line_number  = { fg = '#ff6b6b', bg = '#3a1d22', attr = [] }
insert_line         = { fg = '#f2f4f9', bg = '#14321f', attr = [] }
insert_token        = { fg = '#4ade80', bg = '#1e5638', attr = ['bold'] }
insert_line_number  = { fg = '#4ade80', bg = '#14321f', attr = [] }
common_line         = { fg = '#f2f4f9', bg = '#121418', attr = [] }
common_line_number  = { fg = '#768090', bg = '#121418', attr = [] }
frame               = { fg = '#272f3d', bg = '#121418', attr = [] }
empty_cell          = { fg = '#768090', bg = '#121418', attr = [] }
moved_line          = { fg = '#a371f7', bg = 'none', attr = [] }

[syntax]
comment          = '#737e92'
keyword          = '#f7768e'
operator         = '#89ddff'
punctuation      = '#a9b1d6'
string           = '#9ece6a'
escape           = '#e0af68'
number           = '#ff9e64'
boolean          = '#ff9e64'
constant         = '#ff9e64'
constant_builtin = '#ff9e64'
function         = '#7aa2f7'
method           = '#7aa2f7'
constructor      = '#7aa2f7'
type             = '#2ac3de'
type_builtin     = '#2ac3de'
variable         = '#c0caf5'
parameter        = '#e0af68'
property         = '#7dcfff'
namespace        = '#2ac3de'
tag              = '#f7768e'
attribute        = '#7dcfff'
)foo";

static std::string theme_studio_light_conf =
    R"foo([meta]
name = 'Studio Light'

# Studio Light — a warm off-white light theme with a burnt-orange accent.
# Self-contained: every element sets fg + bg. Changed tokens recolour the
# background (foreground stays dark), like Paper.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#1a1814', bg = '#f8f6f3', attr = [] }
header              = { fg = '#b04e18', bg = '#f0ede8', attr = ['bold', 'underline'] }
delete_line         = { fg = '#1a1814', bg = '#fce4e0', attr = [] }
delete_token        = { fg = '#c0362c', bg = '#f8c4be', attr = ['bold'] }
delete_line_number  = { fg = '#c0362c', bg = '#f6d4cf', attr = [] }
insert_line         = { fg = '#1a1814', bg = '#e6f6e9', attr = [] }
insert_token        = { fg = '#1f7a3d', bg = '#b6e6be', attr = ['bold'] }
insert_line_number  = { fg = '#1f7a3d', bg = '#cdeecf', attr = [] }
common_line         = { fg = '#1a1814', bg = '#f8f6f3', attr = [] }
common_line_number  = { fg = '#8a8480', bg = '#f8f6f3', attr = [] }
frame               = { fg = '#ddd8d2', bg = '#f8f6f3', attr = [] }
empty_cell          = { fg = '#8a8480', bg = '#f8f6f3', attr = [] }
moved_line          = { fg = '#7c3aed', bg = 'none', attr = [] }

[syntax]
comment          = '#6b6460'
keyword          = '#a5341f'
operator         = '#0a4fa8'
punctuation      = '#1a1814'
string           = '#0a3a6b'
escape           = '#0a3a6b'
number           = '#0a4fa8'
boolean          = '#0a4fa8'
constant         = '#0a4fa8'
constant_builtin = '#0a4fa8'
function         = '#7e3fc4'
method           = '#7e3fc4'
constructor      = '#7e3fc4'
type             = '#7e3fc4'
type_builtin     = '#a5341f'
variable         = '#8a3d00'
parameter        = '#1a1814'
property         = '#8a3d00'
namespace        = '#7e3fc4'
tag              = '#116329'
attribute        = '#0a4fa8'
)foo";

static std::string theme_ember_dark_conf =
    R"foo([meta]
name = 'Ember Dark'

# Ember Dark — a warm retro dark theme (inspired by the Gruvbox palette).
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#ebdbb2', bg = '#282828', attr = [] }
header              = { fg = '#fabd2f', bg = '#3c3836', attr = ['bold', 'underline'] }
delete_line         = { fg = '#ebdbb2', bg = '#452a2a', attr = [] }
delete_token        = { fg = '#fb4934', bg = '#3a2020', attr = ['bold'] }
delete_line_number  = { fg = '#fb4934', bg = '#452a2a', attr = [] }
insert_line         = { fg = '#ebdbb2', bg = '#2f331f', attr = [] }
insert_token        = { fg = '#b8bb26', bg = '#26301a', attr = ['bold'] }
insert_line_number  = { fg = '#b8bb26', bg = '#2f331f', attr = [] }
common_line         = { fg = '#ebdbb2', bg = '#282828', attr = [] }
common_line_number  = { fg = '#928374', bg = '#282828', attr = [] }
frame               = { fg = '#504945', bg = '#282828', attr = [] }
empty_cell          = { fg = '#928374', bg = '#282828', attr = [] }
moved_line          = { fg = '#a371f7', bg = 'none', attr = [] }

[syntax]
comment          = '#928374'
keyword          = '#fb4934'
operator         = '#fe8019'
punctuation      = '#ebdbb2'
string           = '#b8bb26'
escape           = '#fe8019'
number           = '#d3869b'
boolean          = '#d3869b'
constant         = '#d3869b'
constant_builtin = '#d3869b'
function         = '#8ec07c'
method           = '#8ec07c'
constructor      = '#8ec07c'
type             = '#fabd2f'
type_builtin     = '#fabd2f'
variable         = '#ebdbb2'
parameter        = '#ebdbb2'
property         = '#83a598'
namespace        = '#fabd2f'
tag              = '#8ec07c'
attribute        = '#b8bb26'
)foo";

static std::string theme_ember_light_conf =
    R"foo([meta]
name = 'Ember Light'

# Ember Light — a warm retro light theme (inspired by the Gruvbox palette).
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#3c3836', bg = '#fbf1c7', attr = [] }
header              = { fg = '#af3a03', bg = '#ebdbb2', attr = ['bold', 'underline'] }
delete_line         = { fg = '#3c3836', bg = '#f7dcd2', attr = [] }
delete_token        = { fg = '#9d0006', bg = '#f2c9be', attr = ['bold'] }
delete_line_number  = { fg = '#9d0006', bg = '#f2c9be', attr = [] }
insert_line         = { fg = '#3c3836', bg = '#e4e8bf', attr = [] }
insert_token        = { fg = '#79740e', bg = '#d5e0a8', attr = ['bold'] }
insert_line_number  = { fg = '#79740e', bg = '#d5e0a8', attr = [] }
common_line         = { fg = '#3c3836', bg = '#fbf1c7', attr = [] }
common_line_number  = { fg = '#7c6f64', bg = '#fbf1c7', attr = [] }
frame               = { fg = '#d5c4a1', bg = '#fbf1c7', attr = [] }
empty_cell          = { fg = '#7c6f64', bg = '#fbf1c7', attr = [] }
moved_line          = { fg = '#7c3aed', bg = 'none', attr = [] }

[syntax]
comment          = '#7c6f64'
keyword          = '#9d0006'
operator         = '#af3a03'
punctuation      = '#3c3836'
string           = '#79740e'
escape           = '#af3a03'
number           = '#8f3f71'
boolean          = '#8f3f71'
constant         = '#8f3f71'
constant_builtin = '#8f3f71'
function         = '#427b58'
method           = '#427b58'
constructor      = '#427b58'
type             = '#8f5502'
type_builtin     = '#8f5502'
variable         = '#3c3836'
parameter        = '#3c3836'
property         = '#076678'
namespace        = '#8f5502'
tag              = '#79740e'
attribute        = '#427b58'
)foo";

static std::string theme_catppuccin_mocha_conf =
    R"foo([meta]
name = 'Catppuccin Mocha'

# Catppuccin Mocha — the community-favourite pastel dark theme. Its keyword is
# mauve (violet), so the moved accent is peach — clearly distinct from the
# keyword and from add-green / delete-red. Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#cdd6f4', bg = '#1e1e2e', attr = [] }
header              = { fg = '#cba6f7', bg = '#313244', attr = ['bold', 'underline'] }
delete_line         = { fg = '#cdd6f4', bg = '#3a2531', attr = [] }
delete_token        = { fg = '#f38ba8', bg = '#562c3a', attr = ['bold'] }
delete_line_number  = { fg = '#f38ba8', bg = '#3a2531', attr = [] }
insert_line         = { fg = '#cdd6f4', bg = '#26352c', attr = [] }
insert_token        = { fg = '#a6e3a1', bg = '#2f4d3a', attr = ['bold'] }
insert_line_number  = { fg = '#a6e3a1', bg = '#26352c', attr = [] }
common_line         = { fg = '#cdd6f4', bg = '#1e1e2e', attr = [] }
common_line_number  = { fg = '#7f849c', bg = '#1e1e2e', attr = [] }
frame               = { fg = '#45475a', bg = '#1e1e2e', attr = [] }
empty_cell          = { fg = '#7f849c', bg = '#1e1e2e', attr = [] }
moved_line          = { fg = '#fab387', bg = 'none', attr = [] }

[syntax]
comment          = '#7f849c'
keyword          = '#cba6f7'
operator         = '#89dceb'
punctuation      = '#cdd6f4'
string           = '#a6e3a1'
escape           = '#89b4fa'
number           = '#fab387'
boolean          = '#fab387'
constant         = '#fab387'
constant_builtin = '#fab387'
function         = '#89b4fa'
method           = '#89b4fa'
constructor      = '#89b4fa'
type             = '#f9e2af'
type_builtin     = '#f9e2af'
variable         = '#cdd6f4'
parameter        = '#eba0ac'
property         = '#89b4fa'
namespace        = '#f9e2af'
tag              = '#cba6f7'
attribute        = '#f9e2af'
)foo";

static std::string theme_catppuccin_latte_conf =
    R"foo([meta]
name = 'Catppuccin Latte'

# Catppuccin Latte — the community-favourite pastel light theme. A soft palette
# by design (some syntax roles sit below strict AA). Keyword is mauve (violet),
# so the moved accent is teal — distinct from keyword and add-green / delete-red.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#4c4f69', bg = '#eff1f5', attr = [] }
header              = { fg = '#8839ef', bg = '#ccd0da', attr = ['bold', 'underline'] }
delete_line         = { fg = '#4c4f69', bg = '#f6d9de', attr = [] }
delete_token        = { fg = '#d20f39', bg = '#f4ccd4', attr = ['bold'] }
delete_line_number  = { fg = '#d20f39', bg = '#f4ccd4', attr = [] }
insert_line         = { fg = '#4c4f69', bg = '#dcecd4', attr = [] }
insert_token        = { fg = '#40a02b', bg = '#cfe6c8', attr = ['bold'] }
insert_line_number  = { fg = '#40a02b', bg = '#cfe6c8', attr = [] }
common_line         = { fg = '#4c4f69', bg = '#eff1f5', attr = [] }
common_line_number  = { fg = '#7c7f93', bg = '#eff1f5', attr = [] }
frame               = { fg = '#bcc0cc', bg = '#eff1f5', attr = [] }
empty_cell          = { fg = '#7c7f93', bg = '#eff1f5', attr = [] }
moved_line          = { fg = '#179299', bg = 'none', attr = [] }

[syntax]
comment          = '#7c7f93'
keyword          = '#8839ef'
operator         = '#04a5e5'
punctuation      = '#4c4f69'
string           = '#40a02b'
escape           = '#1e66f5'
number           = '#fe640b'
boolean          = '#fe640b'
constant         = '#fe640b'
constant_builtin = '#fe640b'
function         = '#1e66f5'
method           = '#1e66f5'
constructor      = '#1e66f5'
type             = '#df8e1d'
type_builtin     = '#df8e1d'
variable         = '#4c4f69'
parameter        = '#e64553'
property         = '#1e66f5'
namespace        = '#df8e1d'
tag              = '#8839ef'
attribute        = '#df8e1d'
)foo";

static std::string theme_tokyo_night_conf =
    R"foo([meta]
name = 'Tokyo Night'

# Tokyo Night — the community-favourite modern dark theme. Its keyword is purple,
# so the moved accent is orange — distinct from keyword and add-green / delete-red.
# Self-contained: every element sets fg + bg.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#c0caf5', bg = '#1a1b26', attr = [] }
header              = { fg = '#7aa2f7', bg = '#292e42', attr = ['bold', 'underline'] }
delete_line         = { fg = '#c0caf5', bg = '#351f2b', attr = [] }
delete_token        = { fg = '#f7768e', bg = '#54293a', attr = ['bold'] }
delete_line_number  = { fg = '#f7768e', bg = '#351f2b', attr = [] }
insert_line         = { fg = '#c0caf5', bg = '#1f3324', attr = [] }
insert_token        = { fg = '#9ece6a', bg = '#2c4b38', attr = ['bold'] }
insert_line_number  = { fg = '#9ece6a', bg = '#1f3324', attr = [] }
common_line         = { fg = '#c0caf5', bg = '#1a1b26', attr = [] }
common_line_number  = { fg = '#7982b3', bg = '#1a1b26', attr = [] }
frame               = { fg = '#3b4261', bg = '#1a1b26', attr = [] }
empty_cell          = { fg = '#7982b3', bg = '#1a1b26', attr = [] }
moved_line          = { fg = '#ff9e64', bg = 'none', attr = [] }

[syntax]
comment          = '#7982b3'
keyword          = '#bb9af7'
operator         = '#89ddff'
punctuation      = '#c0caf5'
string           = '#9ece6a'
escape           = '#89ddff'
number           = '#ff9e64'
boolean          = '#ff9e64'
constant         = '#ff9e64'
constant_builtin = '#ff9e64'
function         = '#7aa2f7'
method           = '#7aa2f7'
constructor      = '#7aa2f7'
type             = '#2ac3de'
type_builtin     = '#2ac3de'
variable         = '#c0caf5'
parameter        = '#e0af68'
property         = '#7dcfff'
namespace        = '#2ac3de'
tag              = '#f7768e'
attribute        = '#bb9af7'
)foo";

static std::string theme_tokyo_night_day_conf =
    R"foo([meta]
name = 'Tokyo Night Day'

# Tokyo Night Day — the light companion to Tokyo Night (a soft light palette;
# some roles sit below strict AA). Keyword is purple, so the moved accent is
# cyan — distinct from keyword and add-green / delete-red.

[settings]
word_wrap = true
show_line_numbers = true
context_colored_line_numbers = true
line_number_align_right = false

[style]
background          = { fg = '#3760bf', bg = '#e1e2e7', attr = [] }
header              = { fg = '#2e7de9', bg = '#d5d9e8', attr = ['bold', 'underline'] }
delete_line         = { fg = '#3760bf', bg = '#f2d5de', attr = [] }
delete_token        = { fg = '#f52a65', bg = '#f2c6d2', attr = ['bold'] }
delete_line_number  = { fg = '#f52a65', bg = '#f2c6d2', attr = [] }
insert_line         = { fg = '#3760bf', bg = '#dbe8cf', attr = [] }
insert_token        = { fg = '#587539', bg = '#cfe0bf', attr = ['bold'] }
insert_line_number  = { fg = '#587539', bg = '#cfe0bf', attr = [] }
common_line         = { fg = '#3760bf', bg = '#e1e2e7', attr = [] }
common_line_number  = { fg = '#656b95', bg = '#e1e2e7', attr = [] }
frame               = { fg = '#a8aecb', bg = '#e1e2e7', attr = [] }
empty_cell          = { fg = '#656b95', bg = '#e1e2e7', attr = [] }
moved_line          = { fg = '#007197', bg = 'none', attr = [] }

[syntax]
comment          = '#656b95'
keyword          = '#9854f1'
operator         = '#007197'
punctuation      = '#3760bf'
string           = '#587539'
escape           = '#007197'
number           = '#b15c00'
boolean          = '#b15c00'
constant         = '#b15c00'
constant_builtin = '#b15c00'
function         = '#2e7de9'
method           = '#2e7de9'
constructor      = '#2e7de9'
type             = '#8c6c3e'
type_builtin     = '#8c6c3e'
variable         = '#3760bf'
parameter        = '#8c6c3e'
property         = '#007197'
namespace        = '#8c6c3e'
tag              = '#f52a65'
attribute        = '#9854f1'
)foo";

enum class ConfigVariableType {
    Bool,
    Int,
    String,
    Color,
};

std::string
diffy::config_get_directory() {
    return fmt::format("{}/diffy", sago::getConfigHome());
}

std::vector<std::pair<std::string, std::string>>
diffy::config_bundled_themes() {
    return {
        {"theme_paper", theme_paper_conf},
        {"theme_ink", theme_ink_conf},
        {"theme_studio_dark", theme_studio_dark_conf},
        {"theme_studio_light", theme_studio_light_conf},
        {"theme_ember_dark", theme_ember_dark_conf},
        {"theme_ember_light", theme_ember_light_conf},
        {"theme_catppuccin_mocha", theme_catppuccin_mocha_conf},
        {"theme_catppuccin_latte", theme_catppuccin_latte_conf},
        {"theme_tokyo_night", theme_tokyo_night_conf},
        {"theme_tokyo_night_day", theme_tokyo_night_day_conf},
    };
}

std::optional<std::string>
diffy::config_theme_display_name(const std::string& conf_text) {
    Value tree;
    ParseResult parse_result;
    if (!cfg_parse_value_tree(conf_text, parse_result, tree) || !parse_result.is_ok())
        return std::nullopt;
    if (auto v = tree.lookup_value_by_path("meta.name"); v && v->get().is_string())
        return v->get().as_string();
    return std::nullopt;
}

// Print a single "Creating initial configuration" header before the first
// created file (lazily, so nothing prints when everything already exists), then
// list each created file's name indented under it.
static void
announce_created_file(const std::string& config_root, const std::string& path) {
    // Diagnostics go to stderr so first-run output never corrupts stdout (a
    // piped unified diff must stay a valid patch; golden snapshots stay stable).
    static bool header_printed = false;
    if (!header_printed) {
        fmt::print(stderr, "Creating initial configuration in {}:\n", config_root);
        header_printed = true;
    }
    fmt::print(stderr, "  {}\n", std::filesystem::path(path).filename().string());
}

// Write the bundled example themes into the config directory, skipping any that
// already exist so user edits are never clobbered.
static void
config_write_bundled_themes(const std::string& config_root) {
    std::error_code ec;
    std::filesystem::create_directories(config_root, ec);
    for (const auto& [name, content] : diffy::config_bundled_themes()) {
        const std::string path = fmt::format("{}/{}.conf", config_root, name);
        if (std::filesystem::exists(path))
            continue;
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            fmt::print(stderr, "warning: could not write bundled theme '{}'\n", path);
            continue;
        }
        announce_created_file(config_root, path);
        fwrite(content.c_str(), content.size(), 1, f);
        fclose(f);
    }
}

enum class ConfigLoadResult {
    Ok,
    Invalid,
    DoesNotExist,
};

ConfigLoadResult
config_load_file(const std::string& config_path,
                 diffy::Value& config_table,
                 diffy::ParseResult& load_result) {
    ConfigLoadResult result = ConfigLoadResult::Ok;

    if (cfg_load_file(config_path, load_result, config_table)) {
        if (config_table.is_table()) {
            result = ConfigLoadResult::Ok;
        } else {
            result = ConfigLoadResult::Invalid;
        }
    } else {
        if (load_result.kind == diffy::ParseErrorKind::File) {
            result = ConfigLoadResult::DoesNotExist;
        } else {
            result = ConfigLoadResult::Invalid;
        }
    }
    return result;
}

static void
config_save(const std::string& config_root, const std::string& config_name, diffy::Value& config_value) {
    std::vector<std::string> required_dirs;
    std::filesystem::path current{config_root};
    while (current.has_root_directory() && !std::filesystem::exists(current)) {
        if (current == std::filesystem::current_path().root_path())
            break;
        required_dirs.push_back(current.string());
        current = current.parent_path();
    }

    for (auto it = required_dirs.rbegin(); it != required_dirs.rend(); ++it) {
        if (!std::filesystem::exists(*it)) {
            std::filesystem::create_directory(*it);
        }
    }

    // Write to a temp file then rename over the target, so a crash mid-write can't
    // truncate/corrupt diffy.conf (shared with the GUI). Check the write, too.
    const std::string tmp = config_name + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) {
        fmt::print(stderr, "Failed to open '{}' for writing.\n", tmp.c_str());
        fmt::print(stderr, "   errno ({}) = {}\n", errno, strerror(errno));
        return;
    }

    std::string serialized = cfg_serialize(config_value);
    bool ok = serialized.empty() || fwrite(serialized.data(), 1, serialized.size(), f) == serialized.size();
    if (fflush(f) != 0)
        ok = false;
    if (fclose(f) != 0)
        ok = false;

    std::error_code ec;
    if (!ok) {
        fmt::print(stderr, "Failed to write '{}'.\n", config_name.c_str());
        std::filesystem::remove(tmp, ec);
        return;
    }
    std::filesystem::rename(tmp, config_name, ec);
    if (ec) {
        fmt::print(stderr, "Failed to replace '{}': {}\n", config_name.c_str(), ec.message());
        std::filesystem::remove(tmp, ec);
    }
}

using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;

static void
config_apply_options(diffy::Value& config, const OptionVector& options) {
    for (const auto& [path, type, ptr] : options) {
        // Do we have a value for this option in the config we loaded?
        if (auto stored_value = config.lookup_value_by_path(path); stored_value) {
            // Yes. So we take the value and write it into our settings struct.
            auto& v = stored_value->get();
            switch (type) {
                case ConfigVariableType::Bool: {
                    if (v.is_bool()) {
                        *((bool*) ptr) = v.as_bool();
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected bool)\n", path);
                    }
                } break;
                case ConfigVariableType::Int: {
                    if (v.is_int()) {
                        *((int64_t*) ptr) = (int64_t) v.as_int();
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected int)\n", path);
                    }
                } break;
                case ConfigVariableType::String: {
                    if (v.is_string()) {
                        *((std::string*) ptr) = v.as_string();
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected string)\n", path);
                    }
                } break;
                case ConfigVariableType::Color: {
                    if (v.is_table()) {
                        auto style = diffy::TermStyle::parse_value(v.as_table());
                        if (style) {
                            *((diffy::TermStyle*) ptr) = *style;
                        }
                    } else {
                        fmt::print(stderr, "warning: config value at '{}' is invalid (expected table)\n", path);
                    }
                } break;
            }
        } else {
            // No such setting in the stored file, so we store the default value
            // from the struct.
            switch (type) {
                case ConfigVariableType::Bool: {
                    diffy::Value v{diffy::Value::Bool{*(bool*) ptr}};
                    config.set_value_at(path, v);
                } break;
                case ConfigVariableType::Int: {
                    int value = *(int64_t*) ptr;
                    diffy::Value v{diffy::Value::Int{value}};
                    config.set_value_at(path, v);
                } break;
                case ConfigVariableType::String: {
                    diffy::Value v{diffy::Value::String{*(std::string*) ptr}};
                    config.set_value_at(path, v);
                } break;
                case ConfigVariableType::Color: {
                    diffy::TermStyle* style = (diffy::TermStyle*) ptr;
                    config.set_value_at(path, style->to_value());
                }
            }
        }
    }
}

void
diffy::config_apply_options(diffy::ProgramOptions& program_options) {
    const std::string config_file_name = "diffy.conf";
    const std::string config_root = diffy::config_get_directory();
    const std::string config_path = fmt::format("{}/{}", config_root, config_file_name);

    bool flush_config_to_disk = false;

    ParseResult config_parse_result;
    Value config_file_table_value;
    switch (config_load_file(config_path, config_file_table_value, config_parse_result)) {
        case ConfigLoadResult::Ok: {
            // yay!
        } break;
        case ConfigLoadResult::Invalid: {
            fmt::print(stderr, "error: {}\n\twhile parsing: {}\n", config_parse_result.error, config_path);
        } break;
        case ConfigLoadResult::DoesNotExist: {
            announce_created_file(config_root, config_path);
            flush_config_to_disk = true;
        } break;
    };

    // Sync up the rest of the configuration with the options structs
    using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;

    std::string algorithm = "patience";

    // clang-format off
    const OptionVector options = {
       { "general.default_algorithm",   ConfigVariableType::String, &algorithm },
       { "general.theme",               ConfigVariableType::String, &program_options.theme },
       { "general.context_lines",       ConfigVariableType::Int,    &program_options.context_lines},
       { "general.ignore_line_endings", ConfigVariableType::Bool,   &program_options.ignore_line_endings },
       { "general.ignore_whitespace",   ConfigVariableType::Bool,   &program_options.ignore_whitespace },
    };
    // clang-format on

    config_apply_options(config_file_table_value, options);

    if (auto algo = algo_from_string(algorithm); algo != Algo::kInvalid) {
        program_options.algorithm = algo;
    }

    config_apply_highlight_overrides();

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        if (config_file_table_value["general"].key_comments.empty()) {
            config_file_table_value["general"].key_comments.push_back(config_doc_general);
        }
        config_save(config_root, config_path, config_file_table_value);
    }
}

void
diffy::config_apply_highlight_overrides() {
    // Honor the DIFFY_CONF_PATH test/sandbox override when locating diffy.conf.
    std::string config_path;
    if (const char* override = std::getenv("DIFFY_CONF_PATH"); override && *override) {
        config_path = override;
    } else {
        config_path = fmt::format("{}/diffy.conf", config_get_directory());
    }

    ParseResult parse_result;
    Value table;
    if (!cfg_load_file(config_path, parse_result, table) || !table.is_table()) {
        return;
    }
    auto section = table.lookup_value_by_path("highlight.extensions");
    if (!section || !section->get().is_table()) {
        return;
    }

    std::vector<std::pair<std::string, Language>> patterns;
    section->get().as_table().for_each([&](const std::string& lang, Value& v) {
        if (v.is_string()) {
            patterns.emplace_back(v.as_string(), lang);
        } else if (v.is_array()) {
            for (auto& entry : v.as_array()) {
                if (entry.is_string()) {
                    patterns.emplace_back(entry.as_string(), lang);
                }
            }
        } else {
            fmt::print(stderr,
                       "warning: config value at 'highlight.extensions.{}' is invalid "
                       "(expected string or array of strings)\n",
                       lang);
        }
    });
    language_set_overrides(std::move(patterns));
}

void
diffy::config_apply_theme(const std::string& theme,
                          diffy::ColumnViewCharacters& cv_char_opts,
                          diffy::ColumnViewSettings& cv_view_opts,
                          diffy::ColumnViewTextStyle& cv_style_opts,
                          diffy::ColumnViewTextStyleEscapeCodes& cv_style_escape_codes) {
    const std::string config_file_name = fmt::format("{}.conf", theme);
    const std::string config_root = diffy::config_get_directory();
    const std::string config_path = fmt::format("{}/{}", config_root, config_file_name);

    bool flush_config_to_disk = false;

    ParseResult config_parse_result;
    Value config_file_table_value;
    switch (config_load_file(config_path, config_file_table_value, config_parse_result)) {
        case ConfigLoadResult::Ok: {
            // yay!
        } break;
        case ConfigLoadResult::Invalid: {
            fmt::print(stderr, "error: {}\n\twhile parsing: {}\n", config_parse_result.error, config_path);
        } break;
        case ConfigLoadResult::DoesNotExist: {
            if (theme == "theme_default") {
                announce_created_file(config_root, config_path);
                flush_config_to_disk = true;
            }
        } break;
    };

    // Ensure the bundled example themes exist (idempotent; skips any already on
    // disk, so user edits are never clobbered). Done on every run, not just
    // first-run, so existing installs pick them up too. Placed after the default
    // theme so the created-files list reads default-first.
    config_write_bundled_themes(config_root);

    if (!config_file_table_value.lookup_value_by_path("settings")) {
        config_file_table_value["settings"] = {Value::Table{}};
    }

    if (auto value = config_file_table_value.lookup_value_by_path("style.empty_line"); value != std::nullopt) {
        config_file_table_value.set_value_at("style.empty_cell", *value);
        config_file_table_value["style"]["empty_cell"].key_comments.push_back(
            "// 1.11 migration: 'style.empty_line' renamed to 'style.empty_cell'");
        config_file_table_value["style"].as_table().remove("empty_line");
        flush_config_to_disk = true;
    }

    // Update the color table
    {
        if (config_file_table_value["color_map"].key_comments.empty()) {
            config_file_table_value["color_map"].key_comments.push_back(color_map_comment);
        }
        if (!config_file_table_value.lookup_value_by_path("color_map.red")) {
            config_file_table_value.set_value_at("color_map.red", {"red"});
        }

        auto& color_values = config_file_table_value.lookup_value_by_path("color_map")->get();

        if (color_values.is_table()) {
            color_values.as_table().for_each([&](const std::string& key, Value& value) {
                if (value.is_string()) {
                    auto term_color = TermColor::parse_value(value);
                    if (term_color) {
                        color_map_set(key, *term_color);
                    }
                }
            });
        }
    }

    // Sync up the rest of the configuration with the options structs
    using OptionVector = std::vector<std::tuple<std::string, ConfigVariableType, void*>>;
    // clang-format off
    const OptionVector options = {
        // side-by-side settings
        { "settings.word_wrap",                    ConfigVariableType::Bool, &cv_view_opts.word_wrap},
        { "settings.show_line_numbers",            ConfigVariableType::Bool, &cv_view_opts.show_line_numbers},
        { "settings.context_colored_line_numbers", ConfigVariableType::Bool, &cv_view_opts.context_colored_line_numbers},
        { "settings.line_number_align_right",      ConfigVariableType::Bool, &cv_view_opts.line_number_align_right},

        // side-by-side theme
        { "chars.column_separator",         ConfigVariableType::String, &cv_char_opts.column_separator },
        { "chars.edge_separator",           ConfigVariableType::String, &cv_char_opts.edge_separator },
        { "chars.tab_replacement",          ConfigVariableType::String, &cv_char_opts.tab_replacement },
        { "chars.cr_replacement",           ConfigVariableType::String, &cv_char_opts.cr_replacement },
        { "chars.lf_replacement",           ConfigVariableType::String, &cv_char_opts.lf_replacement },
        { "chars.crlf_replacement",         ConfigVariableType::String, &cv_char_opts.crlf_replacement },
        { "chars.space_replacement",        ConfigVariableType::String, &cv_char_opts.space_replacement },

        // side-by-side color style
        { "style.background",               ConfigVariableType::Color,  &cv_style_opts.background },
        { "style.header",                   ConfigVariableType::Color,  &cv_style_opts.header },
        { "style.delete_line",              ConfigVariableType::Color,  &cv_style_opts.delete_line },
        { "style.delete_token",             ConfigVariableType::Color,  &cv_style_opts.delete_token },
        { "style.delete_line_number",       ConfigVariableType::Color,  &cv_style_opts.delete_line_number },
        { "style.insert_line",              ConfigVariableType::Color,  &cv_style_opts.insert_line },
        { "style.insert_token",             ConfigVariableType::Color,  &cv_style_opts.insert_token },
        { "style.insert_line_number",       ConfigVariableType::Color,  &cv_style_opts.insert_line_number },
        { "style.common_line",              ConfigVariableType::Color,  &cv_style_opts.common_line },
        { "style.empty_cell",               ConfigVariableType::Color,  &cv_style_opts.empty_cell },
        { "style.common_line_number",       ConfigVariableType::Color,  &cv_style_opts.common_line_number },
        { "style.frame",                    ConfigVariableType::Color,  &cv_style_opts.frame },
        { "style.moved_line",               ConfigVariableType::Color,  &cv_style_opts.moved_line },
    };
    // clang-format on

    config_apply_options(config_file_table_value, options);

    // Set up escape code heper struct values
    const std::vector<std::tuple<TermStyle*, std::string*>> colors = {
        {&cv_style_opts.background, &cv_style_escape_codes.background},
        {&cv_style_opts.header, &cv_style_escape_codes.header},
        {&cv_style_opts.delete_line, &cv_style_escape_codes.delete_line},
        {&cv_style_opts.delete_token, &cv_style_escape_codes.delete_token},
        {&cv_style_opts.delete_line_number, &cv_style_escape_codes.delete_line_number},
        {&cv_style_opts.insert_line, &cv_style_escape_codes.insert_line},
        {&cv_style_opts.insert_token, &cv_style_escape_codes.insert_token},
        {&cv_style_opts.insert_line_number, &cv_style_escape_codes.insert_line_number},
        {&cv_style_opts.common_line, &cv_style_escape_codes.common_line},
        {&cv_style_opts.common_line_number, &cv_style_escape_codes.common_line_number},
        {&cv_style_opts.frame, &cv_style_escape_codes.frame},
        {&cv_style_opts.empty_cell, &cv_style_escape_codes.empty_cell},
        {&cv_style_opts.moved_line, &cv_style_escape_codes.moved_line},
    };

    for (const auto& [source_value, dest_string] : colors) {
        dest_string->assign(source_value->to_ansi());
    }

    // Pick the built-in syntax palette (light vs dark) from the theme's
    // background: a bright background gets the light palette. A theme that leaves
    // the background as the terminal default (no RGB we can read) falls back to
    // dark — matching a typical terminal and the historical behaviour. Only used
    // for groups the theme's [syntax] table below doesn't override.
    {
        auto luminance = [](const TermColor& c) -> std::optional<double> {
            // Only 24-bit colours carry real RGB in r/g/b. For 4-bit/8-bit the
            // fields hold SGR code / palette index numbers (e.g. white bg == code
            // 107), so feeding them to the RGB formula misdetects the theme; treat
            // them as "no readable RGB" and fall back to the dark palette.
            if (c.kind == TermColor::Kind::Color24bit) {
                return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
            }
            return std::nullopt;
        };
        auto l = luminance(cv_style_opts.common_line.bg);
        if (!l)
            l = luminance(cv_style_opts.background.bg);
        cv_view_opts.light_theme = l.has_value() && *l > 140.0;
    }

    // Theme-owned syntax palette: replace any previous overrides with this theme's
    // [syntax] table (group name -> "#rrggbb"). Groups the theme omits fall back
    // to the built-in palette chosen above. Shared by all frontends; a frontend
    // may layer user overrides on top by calling set_syntax_color_override after.
    auto apply_syntax_table = [](Value& tree) -> bool {
        auto syntax = tree.lookup_value_by_path("syntax");
        if (!syntax || !syntax->get().is_table())
            return false;
        bool applied = false;
        syntax->get().as_table().for_each([&](const std::string& key, Value& value) {
            if (!value.is_string())
                return;
            const auto group = highlight_group_from_name(key);
            const auto color = TermColor::parse_string(value.as_string());
            if (group && *group != HighlightGroup::None && color) {
                set_syntax_color_override(*group, HlRgb{color->r, color->g, color->b});
                applied = true;
            }
        });
        return applied;
    };

    clear_syntax_overrides();
    if (!apply_syntax_table(config_file_table_value)) {
        // The on-disk theme predates the [syntax] section (existing installs keep
        // their file; config_write_bundled_themes won't clobber it). Fall back to
        // the matching bundled theme's palette so old configs still get colours.
        for (const auto& [name, content] : diffy::config_bundled_themes()) {
            if (name != theme)
                continue;
            ParseResult pr;
            Value bundled;
            if (cfg_parse_value_tree(content, pr, bundled) && pr.is_ok())
                apply_syntax_table(bundled);
            break;
        }
    }

    // Write the configuration to disk with default settings
    if (flush_config_to_disk) {
        if (config_file_table_value["settings"].key_comments.empty()) {
            config_file_table_value["settings"].key_comments.push_back(config_doc_theme);
        }
        config_save(config_root, config_path, config_file_table_value);
    }
}

diffy::Algo
diffy::algo_from_string(std::string s) {
    if (s == "p" || s == "patience" || s == "default")
        return Algo::kPatience;
    else if (s == "mg" || s == "myers-greedy")
        return Algo::kMyersGreedy;
    else if (s == "ml" || s == "myers-linear")
        return Algo::kMyersLinear;
    return Algo::kInvalid;
}

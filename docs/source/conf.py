# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import sys
from pathlib import Path

# Import shared configuration from ContainerHub
CONTAINER_HUB_CONF = (
    Path(__file__).parent.parent.parent
    / "ExternalLib/Kataglyphis-ContainerHub/docs/source_templates/sphinx-book/conf_base.py"
)
if CONTAINER_HUB_CONF.exists():
    import importlib.util

    spec = importlib.util.spec_from_file_location("conf_base", str(CONTAINER_HUB_CONF))
    if spec and spec.loader:
        conf_base = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(conf_base)

        # Inherit shared extensions and theme settings
        extensions = conf_base.SPHINX_EXTENSIONS.copy()
        html_theme = conf_base.HTML_THEME
        html_theme_options = conf_base.HTML_THEME_OPTIONS.copy()
        html_static_path = conf_base.HTML_STATIC_PATH
        html_css_files = conf_base.HTML_CSS_FILES
    else:
        extensions = ["myst_parser", "sphinx_design"]
        html_theme = "sphinx_book_theme"
        html_theme_options = {
            "repository_url": "https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett",
            "use_repository_button": True,
            "show_navbar_depth": 2,
            "navigation_with_keys": True,
        }
        html_static_path = ["_static"]
        html_css_files = ["css/custom.css"]
else:
    extensions = ["myst_parser", "sphinx_design"]
    html_theme = "sphinx_book_theme"
    html_theme_options = {
        "repository_url": "https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett",
        "use_repository_button": True,
        "show_navbar_depth": 2,
        "navigation_with_keys": True,
    }
    html_static_path = ["_static"]
    html_css_files = ["css/custom.css"]

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "Kataglyphis-Renderer"
copyright = "2024, Jonas Heinle"
author = "Jonas Heinle"
with open("../../version.txt", "r") as f:
    release = f.read().strip()

# -- Project-specific overrides ------------------------------------------------
# Update repository URL for this project
html_theme_options["repository_url"] = (
    "https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett"
)

# -- Add project-specific extensions -------------------------------------------
extensions.extend(
    [
        "breathe",
        "exhale",
        "sphinx.ext.graphviz",
        "sphinx.ext.inheritance_diagram",
    ]
)

# -- Exhale configuration (for C++ API docs) -----------------------------------
exhale_args = {
    "containmentFolder": "./api",
    "rootFileName": "library_root.rst",
    "rootFileTitle": "Library API",
    "doxygenStripFromPath": "../..",
    "createTreeView": True,
    "contentsDirectives": True,
    "exhaleExecutesDoxygen": False,
}

# -- MyST extension configuration -----------------------------------------------
myst_enable_extensions = [
    "dollarmath",
    "amsmath",
    "colon_fence",
    "deflist",
]

# -- Breathe configuration (for Doxygen integration) ---------------------------
breathe_projects = {"Kataglyphis-Renderer": "../../build/build/xml"}
breathe_default_project = "Kataglyphis-Renderer"

# -- General configuration ---------------------------------------------------
templates_path = ["_templates"]
exclude_patterns = []

# -- Graphviz output format ---------------------------------------------------
graphviz_output_format = "svg"

# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "Kataglyphis-Renderer"
copyright = "2024, Jonas Heinle"
author = "Jonas Heinle"
release = "1.3"

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "breathe",
    "myst_parser",
    "exhale",
    "sphinx_design",
    "sphinx.ext.graphviz",
    "sphinx.ext.inheritance_diagram",
]

exhale_args = {
    "containmentFolder": "./api",
    "rootFileName": "library_root.rst",
    "rootFileTitle": "Library API",
    "doxygenStripFromPath": "../..",
    "createTreeView": True,
    "contentsDirectives": True,  # Allows nested folder-like structure
    "exhaleExecutesDoxygen": False,  # (optional) if you already run Doxygen manually
}

myst_enable_extensions = [
    "dollarmath",  # Enables dollar-based math syntax
    "amsmath",  # Supports extended LaTeX math environments
    "colon_fence",  # Allows ::: for directives
    "deflist",  # Enables definition lists
]

breathe_projects = {"Kataglyphis-Renderer": "../../build/build/xml"}
breathe_default_project = "Kataglyphis-Renderer"

templates_path = ["_templates"]
exclude_patterns = []

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "style_nav_header_background": "#6af0ad",
    "palette": "dark",  # Set dark mode as default
    "fixed_sidebar": True,
}
html_static_path = ["_static"]
# Here we assume that the file is at _static/css/custom.css
html_css_files = ["css/custom.css"]

graphviz_output_format = "svg"

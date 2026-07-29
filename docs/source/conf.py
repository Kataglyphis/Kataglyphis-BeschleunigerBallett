# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

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

DOCS_SOURCE_DIR = Path(__file__).resolve().parent
REPO_ROOT = DOCS_SOURCE_DIR.parent.parent


def _find_doxygen_xml_dir() -> Path | None:
    env_override_raw = __import__("os").environ.get("KATAGLYPHIS_DOXYGEN_XML_DIR")
    if env_override_raw:
        env_override = Path(env_override_raw)
        if (env_override / "index.xml").exists():
            return env_override

    candidates = [
        REPO_ROOT / "build" / "build" / "xml",
        REPO_ROOT / "build" / "xml",
        REPO_ROOT / "build-clangcl-debug" / "xml",
        REPO_ROOT / "build-clangcl-release" / "xml",
        REPO_ROOT / "build-clangcl-profile" / "xml",
    ]
    for candidate in candidates:
        if (candidate / "index.xml").exists():
            return candidate

    return None


# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "Kataglyphis-Renderer"
copyright = "2024, Jonas Heinle"
author = "Jonas Heinle"
release = (REPO_ROOT / "version.txt").read_text(encoding="utf-8").strip()

# -- Project-specific overrides ------------------------------------------------
# Update repository URL for this project
html_theme_options["repository_url"] = (
    "https://github.com/Kataglyphis/Kataglyphis-BeschleunigerBallett"
)

# -- Add project-specific extensions -------------------------------------------
extensions.extend(
    [
        "sphinx.ext.graphviz",
        "sphinx.ext.inheritance_diagram",
    ]
)

# -- Extra static payloads ------------------------------------------------------
# Copied verbatim into the site root: the WebGPU/glTF WASM demo (built from
# ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer; see
# docs/webgpu-gltf-rust-plan.md for the rebuild commands).
html_extra_path = ["_webgpu_demo"]

# -- MyST extension configuration -----------------------------------------------
myst_enable_extensions = [
    "dollarmath",
    "amsmath",
    "colon_fence",
    "deflist",
]

# -- Breathe / Exhale configuration (optional C++ API docs) --------------------
doxygen_xml_dir = _find_doxygen_xml_dir()
if doxygen_xml_dir is not None:
    extensions.extend(["breathe", "exhale"])
    breathe_projects = {"Kataglyphis-Renderer": str(doxygen_xml_dir)}
    breathe_default_project = "Kataglyphis-Renderer"
    exhale_args = {
        "containmentFolder": "./api",
        "rootFileName": "library_root.rst",
        "rootFileTitle": "Library API",
        "doxygenStripFromPath": str(REPO_ROOT),
        "createTreeView": True,
        "contentsDirectives": True,
        "exhaleExecutesDoxygen": False,
    }

# -- General configuration ---------------------------------------------------
templates_path = ["_templates"]
exclude_patterns = []

# -- Graphviz output format ---------------------------------------------------
graphviz_output_format = "svg"

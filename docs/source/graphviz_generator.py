from pathlib import Path

STATIC_DIR = Path("_static")
OUTPUT_FILE = Path("graphviz_files.rst")


def format_svg_heading(filename: str) -> str:
    name = Path(filename).stem
    name = name.replace("_8", ".")  # Optional: for Doxygen-style names like `AABB_8cpp`
    name = name.replace("_", " ")
    return name.strip().capitalize()


svg_files = sorted(path.name for path in STATIC_DIR.glob("*.svg"))

with OUTPUT_FILE.open("w", encoding="utf-8") as out:
    out.write("Graphviz Include Graphs\n")
    out.write("=======================\n\n")
    out.write(
        "This page is generated from SVG files copied into ``docs/source/_static``. "
        "It is optional and does not block the main documentation build.\n\n"
    )

    if not svg_files:
        out.write(".. note::\n\n")
        out.write(
            "   No Graphviz SVG files were found in ``docs/source/_static``. "
            "Run the graph generation workflow first, then re-run this script.\n"
        )
    else:
        out.write(".. admonition:: Click to expand all include graphs\n\n")
        out.write("   .. dropdown:: Show All Graphviz Diagrams\n\n")
        out.write("      .. raw:: html\n\n")
        out.write("         <style>\n")
        out.write("         .graphviz-container img {\n")
        out.write("             width: 100%;\n")
        out.write("             height: auto;\n")
        out.write("             margin-bottom: 2em;\n")
        out.write("             border: 1px solid #ccc;\n")
        out.write("             box-shadow: 0 0 8px rgba(0,0,0,0.1);\n")
        out.write("             transition: 0.3s;\n")
        out.write("         }\n")
        out.write("         .graphviz-container img:hover {\n")
        out.write("             box-shadow: 0 0 12px rgba(0,0,0,0.4);\n")
        out.write("         }\n")
        out.write("         .graphviz-heading {\n")
        out.write("             font-weight: bold;\n")
        out.write("             font-size: 1.1em;\n")
        out.write("             margin: 1em 0 0.2em;\n")
        out.write("         }\n")
        out.write("         </style>\n")
        out.write('         <div class="graphviz-container">\n\n')

        for svg in svg_files:
            heading = format_svg_heading(svg)
            out.write(f'         <div class="graphviz-heading">{heading}</div>\n')
            out.write(f'         <a href="_static/{svg}" target="_blank">\n')
            out.write(f'           <img src="_static/{svg}" alt="{svg}">\n')
            out.write("         </a>\n\n")

        out.write("         </div>\n")

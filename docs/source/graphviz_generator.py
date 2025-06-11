import os

STATIC_DIR = "_static"
OUTPUT_FILE = "graphviz_files.rst"


def format_svg_heading(filename: str) -> str:
    # Remove extension and replace underscores with spaces
    name = os.path.splitext(filename)[0]
    name = name.replace("_8", ".")  # Optional: for Doxygen-style names like `AABB_8cpp`
    name = name.replace("_", " ")
    return name.strip().capitalize()


with open(OUTPUT_FILE, "w") as out:
    out.write("Graphviz Include Graphs\n")
    out.write("=======================\n\n")
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

    for svg in sorted(os.listdir(STATIC_DIR)):
        if svg.endswith(".svg"):
            heading = format_svg_heading(svg)
            out.write(f'         <div class="graphviz-heading">{heading}</div>\n')
            out.write(f'         <a href="_static/{svg}" target="_blank">\n')
            out.write(f'           <img src="_static/{svg}" alt="{svg}">\n')
            out.write("         </a>\n\n")

    out.write("         </div>\n")

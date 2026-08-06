Kataglyphis-BeschleunigerBallett Documentation
==============================================

.. rst-class:: hero-section

Build and maintenance guide for Kataglyphis-BeschleunigerBallett.

- Project overview and repository map
- Build, run, packaging, and docs workflow
- Optional generated API and Graphviz reference material

.. grid:: 2
   :gutter: 2

   .. grid-item-card:: Project Overview
      :link: README
      :link-type: doc

      Understand the renderer scope, repository layout, and platform targets.

   .. grid-item-card:: Getting Started
      :link: getting_started
      :link-type: doc

      Clone, configure, build, test, and run the project on Linux or Windows.

   .. grid-item-card:: Documentation Workflow
      :link: documentation_workflow
      :link-type: doc

      Build the Sphinx site, enable API docs, and regenerate Graphviz content.

   .. grid-item-card:: Graphviz Architecture
      :link: graphviz_files
      :link-type: doc

      Open generated include graphs or follow the regeneration steps.

   .. grid-item-card:: API Reference
      :link: api/library_root
      :link-type: doc

      Open the C++ API entry page. When Doxygen XML is available, this page is replaced by the generated reference.

   .. grid-item-card:: WebGPU Demo (live)
      :link: webgpu_demo
      :link-type: doc

      Run the Rust WebGPU/glTF renderer in your browser: PBR, shadows, and ACES tonemapping via WebAssembly.

.. toctree::
   :maxdepth: 2
   :caption: Contents:
   :titlesonly:

   README.md
   getting_started.md
   documentation_workflow.md
   graphviz_files
   wsl2_vulkan
   webgpu_demo.md

.. The Exhale-generated API tree. The card above links to it, but a card is
   not a toctree, so without this entry Sphinx reports "document isn't
   included in any toctree" for api/library_root - and with warnings as
   errors that fails the docs build. Hidden because the card is the intended
   entry point; this only gives the generated pages a parent.
.. toctree::
   :maxdepth: 2
   :hidden:

   api/library_root

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`

WSL2 — Vulkan (NVIDIA) benutzen
================================

Dieser Abschnitt beschreibt kurze, praktische Schritte, um in WSL2 eine NVIDIA‑GPU für Vulkan‑Applikationen zu nutzen. Wenn nach dem Start deiner Anwendung nur "llvmpipe" (Mesa Software‑Renderer) angezeigt wird, fehlt üblicherweise der NVIDIA‑Vulkan‑ICD in der WSL‑Distribution.

Kurzüberblick
-------------

- Ursache: Der Vulkan‑Loader lädt nur Mesa‑ICDs (z. B. `lvp`/`llvmpipe`) weil der NVIDIA‑ICD nicht installiert oder nicht auffindbar ist.
- Lösung: das passende Headless/ICD‑Paket in WSL installieren, WSL neu starten und mit `vulkaninfo` verifizieren.

Voraussetzungen
---------------

- Windows‑Seite: aktueller NVIDIA Treiber mit WSL/WSLg‑Unterstützung (Windows 11 empfohlen). `nvidia-smi` sollte in WSL die Karte anzeigen.
- WSL‑Distribution: Zugriff auf `apt` (oder entsprechender Paketmanager) und Netzwerk/Root‑Rechte.

Schritt‑für‑Schritt (Debian/Ubuntu‑basierte Distros)
------------------------------------------------------

1. Falls nötig: Vulkan‑Tools installieren (für `vulkaninfo`):

.. code-block:: bash

   sudo apt update
   sudo apt install vulkan-tools
   sudo apt install nvidia-driver-595

2. Bestimme die installierte NVIDIA Treiber‑Version (optional):

.. code-block:: bash

   nvidia-smi

   # Notiere die Driver Version (z. B. 595.97) — die Paketnamen enthalten oft die Hauptversionsnummer.

3. Installiere das passende Headless/ICD‑Paket in WSL. Beispiel für Treiberversion 595:

.. code-block:: bash

   # Suche verfügbare Headless‑Pakete
   apt search nvidia-headless | sed -n '1,200p'

   # Beispiel (ersetzen, falls deine Distribution andere Paketnamen zeigt):
   sudo apt install --reinstall nvidia-headless-595 nvidia-utils-595

   # Optional, falls vorhanden:
   sudo apt install libnvidia-gl-595 libnvidia-vulkan-595

4. Beende WSL vollständig (auf der Windows‑Seite) und starte es neu:

.. code-block:: powershell

   wsl --shutdown

   # Dann WSL erneut öffnen (z. B. Terminal neu starten).

5. Verifiziere in WSL, ob der NVIDIA‑ICD sichtbar ist und `vulkaninfo` die GPU zeigt:

.. code-block:: bash

   ls -l /usr/share/vulkan/icd.d
   vulkaninfo | grep -i 'Device Name' -A2

   # oder vollständige Ausgabe
   vulkaninfo | head -n 60

Temporärer Workaround
----------------------

Falls ein Vendor‑JSON bereits existiert, du ihn aber nicht als Standard geladen siehst, kannst du testweise die Variable `VK_ICD_FILENAMES` setzen:

.. code-block:: bash

   export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia.json
   vulkaninfo | head -n 20

Hinweise und Troubleshooting
----------------------------

- Wenn `nvidia-smi` in WSL funktioniert, aber trotzdem nur `llvmpipe` erscheint, fehlt höchstwahrscheinlich das Paket `libnvidia-vulkan-<version>` oder die ICD‑JSON in `/usr/share/vulkan/icd.d`.
- In Container/VM‑Setups brauchst du zusätzlich GPU‑Passthrough oder das `nvidia-container-toolkit`.
- Nach Paketänderungen immer `wsl --shutdown` aus Windows ausführen — ein gewöhnlicher WSL‑Neustart reicht nicht.

Weiterführende Links
--------------------

- NVIDIA WSL Dokumentation: https://developer.nvidia.com/wsl
- Vulkan Loader & ICDs: https://github.com/KhronosGroup/Vulkan-Loader

Wenn du möchtest, kann ich die exakten Paketnamen für deine Distribution suchen und die passenden `apt`‑Befehle vorschlagen — poste dazu einfach `nvidia-smi` und `lsb_release -a` falls du unsicher bist.

# Lizenzübersicht der Open-Source-Abhängigkeiten

Die folgenden Tabellen listen die im Repository genutzten Open-Source-Abhängigkeiten auf (Stand: 2026-08-02). Verifikation: Diese Liste wurde gegen `git submodule status`, `ExternalLib/CMakeLists.txt` (FetchContent), die gevendorten Quellen unter `ExternalLib/` sowie `ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer/Cargo.toml` abgeglichen; jede Lizenzangabe stammt aus der jeweils zitierten, auf der Festplatte gelesenen Lizenzdatei bzw. dem `license`-Feld der Cargo-Metadaten — nicht aus Annahmen. Für rechtsverbindliche Angaben ist immer die zitierte Lizenzdatei selbst maßgeblich.

## Git-Submodule unter `ExternalLib/` (mitgeliefert)

| Projekt | URL | Pin | Lizenz (laut Lizenzdatei) | Geprüfte Datei |
|---|---|---|---|---|
| tinyobjloader | https://github.com/tinyobjloader/tinyobjloader | v2.0.0rc10-73-g45636bd | MIT | `ExternalLib/TINY_OBJ_LOADER/LICENSE` |
| glm | https://github.com/g-truc/glm | 6f14f479 | Dual: "The Happy Bunny License or MIT License" | `ExternalLib/GLM/copying.txt` |
| imgui | https://github.com/ocornut/imgui | v1.92.9b-1-g9b7699f32 | MIT | `ExternalLib/IMGUI/LICENSE.txt` |
| stb | https://github.com/nothings/stb | 2c980bb5 | Dual: MIT oder Public Domain (nach Wahl) | `ExternalLib/STB/LICENSE` |
| glfw | https://github.com/glfw/glfw | 3.5.1 (d9d6f0f1) | Zlib/libpng-Lizenztext | `ExternalLib/GLFW/LICENSE.md` |
| Vulkan Memory Allocator (VMA) | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | 3aa92122 | MIT-Lizenztext (Copyright AMD) | `ExternalLib/VULKAN_MEMORY_ALLOCATOR/LICENSE.txt` |
| nlohmann/json | https://github.com/nlohmann/json | v3.11.2-532-g2222d386c | MIT | `ExternalLib/NLOHMANN_JSON/LICENSE.MIT` |
| google/benchmark | https://github.com/google/benchmark | v1.9.4-213-g3e57f2b | Apache-2.0 | `ExternalLib/GOOGLE_BENCHMARK/LICENSE` |
| spdlog | https://github.com/gabime/spdlog | 989d28dd | MIT; enthält gebündeltes {fmt} (MIT) | `ExternalLib/SPDLOG/LICENSE`; `ExternalLib/SPDLOG/include/spdlog/fmt/bundled/fmt.license.rst` |
| google/fuzztest | https://github.com/google/fuzztest | 704efb34 (2026-06-29) | Apache-2.0; zusätzliche Lucent-Notiz für `fuzztest/internal/domains/rune.*` | `ExternalLib/FUZZTEST/LICENSE` |
| kompute (nur optionales Playground, `KATAGLYPHIS_BUILD_KOMPUTE_PLAYGROUND`) | https://github.com/KomputeProject/kompute | v0.9.0-97-g890c97e | Apache-2.0 | `ExternalLib/KOMPUTE/LICENSE` |
| cgltf | https://github.com/jkuhlmann/cgltf | v1.15-11-g85cd623 | MIT-Lizenztext (Copyright Johannes Kuhlmann) | `ExternalLib/cgltf/LICENSE` |
| tomlplusplus | https://github.com/marzer/tomlplusplus | v3.4.0-50-g1e8829b | MIT | `ExternalLib/tomlplusplus/LICENSE` |

## Gevendorte Quellen (kein Submodul)

| Projekt | URL | Lizenz (laut Datei-Headern) | Geprüfte Datei(en) |
|---|---|---|---|
| KTX (KTX-Software, Teil-Vendoring: `include/`, `lib/`, `other_include/`) | https://github.com/KhronosGroup/KTX-Software | Apache-2.0 (SPDX-Header in `include/ktx.h` und in 98 Dateien unter `lib/`) | `ExternalLib/KTX/include/ktx.h` |

Hinweis KTX: Die gevendorte `lib/` enthält eingebettete Third-Party-Komponenten ohne SPDX-Header (u.a. basisu, zstd, lodepng, stb_image, tinyexr, jpgd, etcdec) mit eigenen Lizenzhinweisen in den Dateiköpfen; die vollständige `LICENSE.md` von KTX-Software ist nicht mitvendort. Siehe Abschnitt "Unverifiziert".

## Build-Zeit-Abhängigkeiten via FetchContent (`ExternalLib/CMakeLists.txt`, nicht im Repo eingecheckt)

Lizenz jeweils aus der LICENSE-Datei des lokalen FetchContent-Checkouts unter `build*/\_deps/` gelesen.

| Projekt | URL | Pin | Lizenz (laut Lizenzdatei) | Geprüfte Datei |
|---|---|---|---|---|
| abseil-cpp | https://github.com/abseil/abseil-cpp | 20260526.0 (`ABSL_TAG`) | Apache-2.0 | `build-clangcl-profile/_deps/abseil-cpp-src/LICENSE` |
| googletest (nur `BUILD_TESTING`) | https://github.com/google/googletest | 56efe398 (URL-Pin) | BSD-3-Clause-Lizenztext (Copyright Google Inc.) | `build-clangcl-profile/_deps/googletest-src/LICENSE` |
| Microsoft GSL | https://github.com/microsoft/GSL | v4.2.1 | MIT | `build-clangcl-profile/_deps/gsl-src/LICENSE` |
| Corrosion (nur `RUST_FEATURES`, Build-Tool) | https://github.com/corrosion-rs/corrosion | master | MIT | `build-clangcl-profile/_deps/corrosion-src/LICENSE` |
| ANTLR4 C++ Runtime (transitiv via FuzzTest) | https://github.com/antlr/antlr4 | von FuzzTest gepinnt | BSD-3-Clause-Lizenztext (The ANTLR Project) | `build-linux-local/_deps/antlr_cpp-src/LICENSE.txt` |
| RE2 (transitiv via FuzzTest) | https://github.com/google/re2 | von FuzzTest gepinnt | BSD-3-Clause-Lizenztext (The RE2 Authors) | `build-linux-local/_deps/re2-src/LICENSE` |

## Interne / projekt-spezifische Submodule

| Projekt | URL | Pin | Lizenz | Geprüfte Quelle |
|---|---|---|---|---|
| Kataglyphis-RustProjectTemplate | https://github.com/Kataglyphis/Kataglyphis-RustProjectTemplate | 49e8ee24 (develop) | MIT (`license = "MIT"` in `[workspace.package]`; keine LICENSE-Datei im Submodul) | `ExternalLib/Kataglyphis-RustProjectTemplate/Cargo.toml` |
| Kataglyphis-ContainerHub | https://github.com/Kataglyphis/Kataglyphis-ContainerHub | 8687f0c7 (main) | unverifiziert — keine LICENSE-Datei im Submodul gefunden | — |

## Rust-Crate-Abhängigkeiten (`ExternalLib/Kataglyphis-RustProjectTemplate/crates/webgpu_renderer/Cargo.toml`, `[dependencies]`)

Versionen laut `ExternalLib/Kataglyphis-RustProjectTemplate/Cargo.lock`. Lizenz nur angegeben, wo eine lokale Registry-Kopie (`~/.cargo/registry/src/`) gelesen werden konnte; die Crate-Builds laufen im Container, daher ist der Host-Cache unvollständig.

| Crate | Version (Lock) | Lizenz (`license`-Feld) | Geprüfte Quelle |
|---|---|---|---|
| anyhow | 1.0.104 | MIT OR Apache-2.0 | `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anyhow-1.0.100/Cargo.toml` (Registry-Kopie 1.0.100) |
| log | 0.4.33 | MIT OR Apache-2.0 | `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/log-0.4.29/Cargo.toml` (Registry-Kopie 0.4.29) |
| bytemuck | 1.25.2 | Zlib OR Apache-2.0 OR MIT | `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/bytemuck-1.24.0/Cargo.toml` (Registry-Kopie 1.24.0) |
| env_logger | 0.11.11 | unverifiziert — keine lokale Registry-Kopie | — |
| wgpu | 29.0.4 | unverifiziert — keine lokale Registry-Kopie | — |
| winit | 0.30.13 | unverifiziert — keine lokale Registry-Kopie | — |
| pollster | 0.4.0 | unverifiziert — keine lokale Registry-Kopie | — |
| glam | 0.30.10 | unverifiziert — keine lokale Registry-Kopie | — |
| gltf | 1.4.1 | unverifiziert — keine lokale Registry-Kopie | — |
| bevy_mikktspace | 1.0.0 | unverifiziert — keine lokale Registry-Kopie | — |
| egui | 0.35.0 | unverifiziert — keine lokale Registry-Kopie | — |
| egui-wgpu | 0.35.0 | unverifiziert — keine lokale Registry-Kopie | — |
| egui-winit | 0.35.0 | unverifiziert — keine lokale Registry-Kopie | — |
| ktx2 | 0.5.0 | unverifiziert — keine lokale Registry-Kopie | — |

## Toolchain (Build-Zeit, nicht mit ausgeliefert)

| Tool | Herkunft | Lizenz |
|---|---|---|
| slangc (Slang-Shader-Compiler) | Aus dem installierten Vulkan SDK (`VULKAN_SDK\Bin` bzw. `PATH`), siehe `Scripts/Windows/compile-slang-shaders.ps1` / `Scripts/Linux/compile-slang-shaders.sh`; nicht im Repo gevendort | Toolchain, nicht mitgeliefert; Lizenz nicht aus dem Repo verifizierbar |

## Unverifiziert — zu prüfen

- **Kataglyphis-ContainerHub**: kein LICENSE/COPYING im Submodul vorhanden; Lizenz im Upstream-Repository klären.
- **Rust-Crates ohne lokale Registry-Kopie** (siehe Tabelle oben): env_logger, wgpu, winit, pollster, glam, gltf, bevy_mikktspace, egui, egui-wgpu, egui-winit, ktx2 — Lizenzfelder gegen crates.io bzw. einen vollständigen Cargo-Cache verifizieren.
- **Eingebettete Third-Party-Anteile in `ExternalLib/KTX/lib/`** ohne SPDX-Header (basisu, zstd, lodepng, stb_image, tinyexr, jpgd, etcdec u.a.): Lizenzköpfe der einzelnen Dateien bzw. die Upstream-`LICENSE.md` von KTX-Software prüfen.
- **Rust-Crates bei anderen `anyhow`/`log`/`bytemuck`-Versionen**: Lizenzfeld wurde aus einer benachbarten, lokal gecachten Version gelesen (siehe Tabelle); bei Bedarf gegen die exakte Lock-Version verifizieren.

## Hinweise

- Entfernt gegenüber Stand 2026-03-26: **glad** (kein `ExternalLib/glad`-Submodul mehr vorhanden, keine glad-/OpenGL-Loader-Referenzen unter `Src/`) und **KTX als Submodul** (jetzt Teil-Vendoring, siehe oben).
- Kompute wird nur mit `KATAGLYPHIS_BUILD_KOMPUTE_PLAYGROUND=ON` gebaut (Demo, nicht Teil der Engine).
- Die `build*/_deps/`- und `~/.cargo/`-Pfade sind lokale Checkouts/Caches (nicht eingecheckt); sie dokumentieren, welche Datei bei der Verifikation tatsächlich gelesen wurde.

---
Erstellt automatisch im Repository auf Anforderung; zuletzt vollständig gegen den Datenträger verifiziert am 2026-08-02.

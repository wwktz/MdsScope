# MdsScope architecture

MdsScope is built as a set of one-way CMake modules. Dependencies point
toward lower-level code only:

```text
MdsScope executable
└── mdsscope_ui
    ├── mdsscope_ui_support
    ├── mdsscope_services
    ├── mdsscope_ssh
    ├── mdsscope_plot
    └── mdsscope_mds
        └── mdsscope_core
```

`mdsscope_plot` depends directly on `mdsscope_core`.
`mdsscope_services` depends on `mdsscope_core` and `mdsscope_mds`.
`mdsscope_ssh` depends on `mdsscope_core` and `mdsscope_mds`.
There are no dependency cycles.

## Module responsibilities

- `src/core`: domain value types, paths, configuration parsing and atomic
  persistence, authentication persistence, text helpers, and shot metadata.
- `src/mds`: the small `mds_client.hpp` public facade. Protocol, socket,
  sampling, and connection-pool details live under `src/mds/internal` and
  cannot be included by other modules.
- `src/ssh`: SSH settings, tunnels, and SSH diagnostics.
- `src/services`: application use cases that combine lower-level modules,
  such as background data export.
- `src/ui/plot`: the reusable plot widget and its interaction/rendering code.
- `mdsscope_ui_support`: reusable application dialogs and icon rendering from
  `src/ui`.
- `src/ui/main_window`: presentation coordination. It maps user actions to
  services and updates widgets; file serialization and export execution do
  not live here. `RefreshCoordinator`, `ShotWorkflow`, `UserPreferences`, and
  `SshTunnelManager` own their respective state and lifecycle instead of
  storing those workflows directly in `MainWindow`.
- `src/app`: the composition root and benchmark entry point. Login bootstrap,
  runtime paths, platform integration, and theme runtime are separate
  components.

## Boundary rules

- Production code must not recreate an omnibus internal header.
- Core must not include MDS, SSH, service, or UI headers.
- MDS must not include SSH, service, or UI headers.
- SSH and services must not include UI headers.
- Plot code must not depend on main-window, SSH, service, or MDS code.
- Code outside `src/mds` must not include `mds/internal` headers.
- Headers declare interfaces; substantial implementations stay in `.cpp`
  files. Private widget implementations use owning RAII pointers.
- Configuration and credential replacement uses `QSaveFile` and commits only
  after the complete stream/write status has been checked.
- Theme objects are owned by `QApplication`; font settings are values owned by
  `MainWindow` and passed explicitly to plots and dialogs. Neither uses a
  process-wide mutable pointer or settings singleton.

`cmake/check_module_boundaries.cmake` enforces the include rules in the
internal test suite. CMake target linkage enforces the corresponding link
boundaries.

## Build profiles

The normal build keeps the compiler's default warning policy. Configure with
`-DMDS_SCOPE_STRICT_COMPILE=ON` to enable the project warning gate:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror
```

MSVC uses `/W4 /WX /permissive- /Zc:__cplusplus`.

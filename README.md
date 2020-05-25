# bmp\_undelete

Recovers a deleted BMP file from an ext2/3 filesystem.
Has a CLI/one-shot version as well as an ncurses based TUI version.
It has been tested to be able to recover multiple files in one run.

# Build

 * `cd` into the source directory.
 * To build everything, run `make all`.
 * To build just the CLI, run `make bmp_undelete_cli`.
 * To build just the TUI, run `make bmp_undelete_tui`.

Requires ncurses to be installed to build the TUI.

# Run

For the TUI, just run `./bmp_undelete_tui`.
Everything else is done through the interface.

For the CLI, run `./bmp_undelete_cli [target]`.
The `[target]` parameter is the block device to target (eg, `/dev/sdb1`).

For best results if testing, a fresh filesystem is recommended.

# Disclaimer

 * This is just a proof of concept created as a project.
 * I cannot guarantee that it won't ever misbehave.
 * I do not recommend running it on any important filesystems.
 * I do not recommend running it on any non-ext filesystems.
 * I hold no responsibility for accidentally lost data.

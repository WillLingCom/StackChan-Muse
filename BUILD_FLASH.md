# Build and flash

The current publication is **BLOCKED** on permission for the original avatar assets. No replacement visuals are installed and a clean build is not claimed.

Use the build, private-configuration, pairing and flash steps in [README.md](README.md). `scripts/k151.sh build-public` is the single entry point; it currently stops at the asset readiness check. See [asset preparation](docs/ASSET_PREPARATION.md), [dependency notices](THIRD_PARTY_NOTICES.md) and [validation](docs/VALIDATION.md).

The scripts use an activated ESP-IDF v6.0.1 environment or `IDF_EXPORT` / `IDF_PATH`, not a developer's absolute path. Build caches, temporary files, configuration and logs stay inside this checkout and are ignored. Hardware flashing requires an explicit port and your own device identity; publication preparation never flashes automatically.

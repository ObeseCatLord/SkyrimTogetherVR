# Skyrim Together VR Versioning

Skyrim Together VR uses an independent tag namespace:

```text
stvr-vMAJOR.MINOR.PATCH[-alpha.N|-beta.N|-rc.N]
```

The `stvr-` prefix avoids collisions with the inherited Skyrim Together tags.
The version describes the VR port, not the upstream Skyrim Together release.

- `alpha`: installation, startup, connection, or partial synchronization is
  ready for controlled testing; crashes, missing synchronization, and protocol
  changes are expected.
- `beta`: the supported gameplay scope works end to end and the remaining work
  is compatibility, reliability, or polish.
- `rc`: release candidate with no known blocker in the declared support scope.
- stable: the declared release scope has passed the Windows and Linux test
  matrices and no known data-corruption or repeatable crash blocker remains.

Increment `PATCH` for compatible fixes, `MINOR` when adding a substantial VR
capability or expanding the supported test matrix, and `MAJOR` for an
intentional protocol or packaging break that requires coordinated deployment.
Prerelease counters are monotonic within the same base version.

Every release records both the tagged source revision and the source revision
that produced the packaged binaries. They can differ when documentation and
packaging are finalized after the Windows build. Server revisions are recorded
separately; a revision mismatch is acceptable only when the shared protocol and
server code are unchanged.

## Release Procedure

Create the commit and local release tag before packaging so the handoff Git
bundle is self-contained:

```bash
git tag stvr-v0.1.0-alpha.1
python3 Tools/SkyrimVR/create_prerelease_bundle.py \
  --version stvr-v0.1.0-alpha.1 \
  --build-dir /path/to/tested/windows-package \
  --output-dir artifacts/SkyrimTogetherVR/prerelease/stvr-v0.1.0-alpha.1 \
  --cef-license ThirdParty/CEF-LICENSE.txt \
  --public-fact serverSourceRevision=99d1e1d1 \
  --public-fact serverImage=skyrim-together-vr-server:99d1e1d1
python3 Tools/SkyrimVR/audit_prerelease_bundle.py \
  --asset-dir artifacts/SkyrimTogetherVR/prerelease/stvr-v0.1.0-alpha.1 \
  --expect-version stvr-v0.1.0-alpha.1 \
  --require-server-endpoint incidentalstoat.xyz:26099
(cd artifacts/SkyrimTogetherVR/prerelease/stvr-v0.1.0-alpha.1 && sha256sum -c SHA256SUMS.txt)
```

Only push the tag and create the GitHub prerelease after all three checks pass.

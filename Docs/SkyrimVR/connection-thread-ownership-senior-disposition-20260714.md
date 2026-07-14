# Connection Thread Ownership Senior Review Disposition

The original deadlock premise was disproved. The same transport code connected
successfully after the Realm of Lorkhan modal was accepted and the server moved
from cloud-blocked UDP 10578 to reachable UDP 26099.

| Recommendation | Disposition | Implementation |
|---|---|---|
| Reject transport deadlock as the incident cause. | Adopted | No libuv, GameNetworkingSockets, reconnect, or transport-thread refactor was made. |
| Accept only the verified Realm modal. | Adopted with tighter matching | `devbench_new_game.py` requires normalized `someplace unknown` and `outside of time and space`, exactly one `Begin` button, and a non-cancel index. Unknown dialogs fail closed with their description. |
| Drain modal state before connecting. | Adopted | The driver requires two consecutive closed polls and repeatedly inspects any following modal. |
| Prove cadence resumed before writing the command. | Adopted | The driver requires two increasing successful `task_run` sequences newer than the pre-drain baseline in `SkyrimTogetherVRTickBridge.log`. |
| Force Papyrus/native cadence through modal menus. | Rejected | Existing one-shot registration resumed unchanged after the modal closed. Forcing `World::Update` from an unrelated timer would create an unsafe phase. |
| Treat rotating SKSE worker ownership as this failure. | Rejected for this incident; retained risk | Successful DNS/authentication across rotating workers disproves it as the immediate failure. Engine mutation still requires a verified game/post-animation phase. |
| Require exterior-grid sync while still in Realm of Lorkhan. | Adapted | Default automation now accepts a fresh interior or exterior cell request. `--require-exterior-grid` opts into the stronger exterior/grid proof after leaving Realm. |

The server-specific Linux launcher now defaults to
`incidentalstoat.xyz:26099`. The upstream protocol/server default remains UDP
10578 for normal installations.

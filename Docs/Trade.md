# Player trade (12340)

The client uses the original TradeFrame Lua/XML from the user's MPQ files.
The network implementation preserves seven trade positions, including the
nontraded service position, physical item GUIDs and the partner's state index.
No extracted Blizzard interface assets are stored in this repository.

## Offers and server state

As in the original client, validated local requests immediately update the
local offer and lock the selected inventory item. AzerothCore normally sends
extended item and money updates only to the other party. Waiting for an own-side
extended acknowledgement would leave the local window empty indefinitely.
Optional own-side snapshots reconcile the local view. Offering an item or money
does not change physical inventory or coinage; those changes come from the
server when both parties accept.

An uncached item description retains the intended position until metadata is
available. Splitting a stack waits for the actual server-created stack and the
matching source quantity. Swap/remove/cancel release the appropriate selections.
Bound and quest items, nonempty containers and equipped bags are checked before
offering a transferable item. The seventh position permits service items.

Extended updates are parsed atomically as sparse 73-byte records, rather than
treating state indices as slot counts. Truncated, duplicate-slot, wrong-trade
and stale-revision packets preserve the previous valid state. Changed offers
revoke acceptance; accept requests carry the counterpart's state index.
Cancel, closure, death, disconnect and timed-out prerequisite operations clear
the trade state.

The original money fields support numeric editing and a distinct money cursor.
Tooltips expose the actual item and durability. Links retain enchantments,
gem item IDs and random-property metadata. Item-targeting spells support the
nontraded slot with TARGET_FLAG_TRADE_ITEM and packed target 6. Replacing a
permanent enchant uses the original confirmation event; the server validates
spell requirements, reagents and the final operation.

## Verification

Isolated tests cover complete request payloads, sparse/partial/stale packets,
item locks, cold metadata, service targets and real Lua argument types. Native
verification uses an ephemeral QA account on the project's loopback realm,
with native authentication and Warden. Original items are only offered in
cancelled trades; completed transfers use a newly created test item and a
bounded money grant. Cleanup deletes the QA character through the protocol,
removes its account and checks the original inventory and balance.

The native peer refuses to accept transfers of original inventory items.
Service-enchant packets and replacement confirmation are tested in isolation;
verification does not grant professions or enchant spells to the user's character.
The completed native round trip covers 74 steps and seven nonempty engine
captures, including real split/merge, changed-offer consent and typed money.

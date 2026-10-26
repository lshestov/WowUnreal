# Mail (12340)

The client loads the original `MailFrame.lua/xml` from the user's MPQ data.
`LuaMailApi.cpp` exposes inbox, composer, stationery, package and invoice APIs;
`WowMail.cpp` owns mailbox access and transactions. No extracted interface assets
are included in the repository.

## State and item identity

Only one send/take/return/delete/copy operation is pending at a time. Inbox rows
are addressed by immutable server message IDs. Taking an attachment uses its
item GUID, independently of sparse attachment positions. Counts, money and
removal change after a matching, complete server acknowledgement. Incomplete
inbox/status packets retain the previous complete cache.

Composer attachments reference real carried item GUIDs and lock their bag icons.
A split attachment first creates an actual server stack; it becomes selectable
only after both source quantity and destination identity agree. Full backpacks
can use free slots in equipped bags. Closing the composer releases selections;
it does not destroy physical stacks. Refundable items require the original
confirmation before sending.

The mailbox closes on death, disconnect or leaving interaction range. Mail list
refreshes observe the native one-minute interval, and pending delivery status
drives the minimap notification. Sender queries and item template queries update
the stock window when their responses arrive.

## Presentation dependencies

MailTemplate DBC has separate localized subject and body blocks. Stationery DBC
provides the open letter's background. Stock `SetPortraitToTexture` accepts
either a texture object or its global name, including an unavailable item icon.
EditBox FontString inheritance applies font size and color to native editors;
pointer dispatch focuses the selected editor. Currency helpers expose the six
native coin icons and localized gold/silver/copper strings. Label rendering
resolves `|4singular:plural;` without changing Lua's original text.

## Verification

`WowUnreal.UI.MailPackets` exercises request bytes, sparse identities, serialized
acknowledgements, return/copy, sequential automatic collection, split validation,
equipped-bag space and refund consent. `WowUnreal.Hud.MailApi` checks original Lua
tuples, DBC data, invoices and currency helpers; editor and label regressions cover
the shared rendering dependencies. The mailbox parser test verifies atomic
handling of truncated inbox data.

Native verification uses the account's own character and GM mailbox service.
Temporary messages contain a body, one temporary item or test copper; scenarios
read, take and delete them and restore the original balance and inventory. The
original inbox message is preserved. Composer verification restores a split
stack and checks the server's self-recipient rejection. Positive send, return
and permanent-copy acknowledgements are covered in isolation; native checks do
not send mail to other user characters or players.

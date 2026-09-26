// ut_recon.h - the journal's pending rows settled against the character's OWN containers.
//
// A deposit writes its row "pending in" and a take "pending out"; the engine's move reaches the
// character file only at the next Player.chr save. When a load finds a row a save's time cannot
// settle (the game was killed, or the watch never saw the save), the journal keeps it UNRESOLVED
// (ut_rescue.h, journalOpenSet `defer`) and this module looks for the row's item, by replica
// identity (utReplicaSame: the same read the deposit made, Item::GetItemReplicaInfo), in:
//   * the inventory sacks   ControllerPlayer::GetInventoryCtrl -> PlayerInventoryCtrl::
//                           GetNumberOfSacks / GetSack(i) -> InventorySack::GetInventory (the id
//                           map the owned walk reads)
//   * the equipment         ControllerCharacter::GetEquipmentCtrl -> EquipmentCtrl::GetItem_<slot>
//                           (ten getters) + the other weapon set (the hand getters' own fields)
//   * at a caravan open     GameEngine's Stash / Transfer / Relic Vault sacks (the real ones,
//                           the same offsets the owned walk uses; the mod sack is never read as
//                           a container: its ids are excluded)
// and hands utPendingVerdict's answer to journalApplyVerdicts (one write, one INFO per row).
// Everything is READ ONLY, inside SEH guards, bounded. GAME THREAD ONLY.
#pragma once

namespace ut {

// World up, after the set was chosen (viewSelectSet): the inventory and the equipment. A row
// whose item IS there is decided; the others wait for the caravan (its sacks are loaded there).
void reconOnWorld();
// Every caravan open while a row is unresolved (the NpcCaravan::OnPlayerInteract detour, after
// the engine's own call):
// every container. A row found nowhere is decided; a check that cannot be made falls back to
// today's rule (journalUnresolvedFallback). Nothing is done while no row is unresolved.
void reconOnCaravan();

}  // namespace ut

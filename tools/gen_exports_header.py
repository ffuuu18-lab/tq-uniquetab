"""Generate src/tq_exports.h from the Engine.dll / Game.dll export dumps.

The dumps are build/exports/exports_<module>.txt, written by tools/tqexports.py from the game
folder (HANDOFF.md section 5 has the two steps); --exports points at another copy of them.

    python gen_exports_header.py [--exports <dir>] [--out <file>] [--undname <exe>] [--check]

Every export the mod may resolve with GetProcAddress or hook is emitted as

    // public: void __thiscall GAME::GameEngine::SetCaravanMode(int)
    #define TQ_GAMEENGINE_SETCARAVANMODE "?SetCaravanMode@GameEngine@GAME@@QAEXH@Z"
    #define TQ_GAMEENGINE_SETCARAVANMODE_MOD "Game.dll"
    #define TQ_GAMEENGINE_SETCARAVANMODE_CC TQCC_THISCALL
    #define TQ_GAMEENGINE_SETCARAVANMODE_SRET 0
    #define TQ_GAMEENGINE_SETCARAVANMODE_REQ 1

The mangled names are checked verbatim against the dumps (never typed by hand: a name that is not
in its module's dump stops the generator) and the comment is undname.exe's output. Titan Quest is
x86, where the calling convention is NOT the same for every member (x64 collapses them all), so the
convention and a by-value class return (a hidden return pointer) are read out of the MANGLING; the
mod static_asserts each typedef against _CC.
_REQ 1 = the mod is OFF when the name does not resolve (the GD REQUIRED flag), 0 = optional.

--check regenerates in memory and exits 1 when the file on disk differs.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_EXPORTS = os.path.join(ROOT, "build", "exports")
DEFAULT_OUT = os.path.join(ROOT, "src", "tq_exports.h")


def _find_undname():
    """undname.exe out of whichever MSVC toolchain is installed - never a written-down path."""
    found = shutil.which("undname")
    if found:
        return found
    vswhere = ""
    for var in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(var)
        if not base:
            continue
        cand = os.path.join(base, "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if os.path.exists(cand):
            vswhere = cand
            break
    if not vswhere:
        return ""
    try:
        out = subprocess.run(
            [vswhere, "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            check=True, stdout=subprocess.PIPE,
        ).stdout.decode("utf-8", "replace").strip().splitlines()
    except (OSError, subprocess.CalledProcessError):
        return ""
    if not out:
        return ""
    tools = os.path.join(out[0], "VC", "Tools", "MSVC")
    if not os.path.isdir(tools):
        return ""
    for ver in sorted(os.listdir(tools), reverse=True):
        for host in (("Hostx64", "x86"), ("Hostx64", "x64"), ("Hostx86", "x86")):
            cand = os.path.join(tools, ver, "bin", host[0], host[1], "undname.exe")
            if os.path.exists(cand):
                return cand
    return ""


DEFAULT_UNDNAME = _find_undname()

STR = "?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@"
REQ, OPT = 1, 0
# std::vector<GAME::GameTextLine> (VS2012), the rollover's line vector.
TIPVEC = "?$vector@UGameTextLine@GAME@@V?$allocator@UGameTextLine@GAME@@@std@@@std@@"

# (section title, module, macro prefix, [(macro suffix, decorated name, REQ|OPT), ...]).
# TQ binds a FIXED list (the skeleton + the design-v2 hook set), so each section names its
# exports exactly; the strings are the ones resolved live in the game.
SELECTION = [
    ("Engine - singleton, frame, database", "Engine.dll", "TQ_ENGINE", [
        ("GENGINE", "?gEngine@GAME@@3PAVEngine@1@A", REQ),
        ("PRESENTSURFACE", "?PresentSurface@Engine@GAME@@QAEXXZ", REQ),
        ("LOADMAINDATABASE", "?LoadMainDatabase@Engine@GAME@@AAEXXZ", REQ),
        ("LOADDATABASE", "?LoadDatabase@Engine@GAME@@AAE_NABV" + STR + "@Z", REQ),
        ("GETDATABASEARCHIVECHECKSUM", "?GetDatabaseArchiveChecksum@Engine@GAME@@QAEIXZ", REQ),
        ("HASLOADEDCUSTOMDATABASE", "?HasLoadedCustomDatabase@Engine@GAME@@QAE_NXZ", OPT),
        ("INITIALIZEMOD", "?InitializeMod@Engine@GAME@@QAE_NABV" + STR + "0@Z", OPT),
        ("GETGAMEINFO", "?GetGameInfo@Engine@GAME@@QAEPAVGameInfo@2@XZ", OPT),
        # TQ has no GameInfo::IsNetworkClient/Server: the two live on Engine.
        ("ISNETWORKCLIENT", "?IsNetworkClient@Engine@GAME@@QAE_NXZ", OPT),
        ("ISNETWORKSERVER", "?IsNetworkServer@Engine@GAME@@QAE_NXZ", OPT),
        ("GETGRAPHICSENGINE", "?GetGraphicsEngine@Engine@GAME@@QBEPAVGraphicsEngine@2@XZ", OPT),
        # the gray icons - the engine's file system gains the mod's gray\ folder as a
        # DIRECTORY source, the way TQ.exe adds "./Settings/" (AddSource(1, dir, 0, 0, 0, 0) at
        # TQ.exe 0x4AAB5); FileSystem::OpenFile then finds gray\ug*.tex by its plain name.
        ("GETFILESYSTEM", "?GetFileSystem@Engine@GAME@@QAEPAVFileSystem@2@XZ", OPT),
        ("FS_ADDSOURCE", "?AddSource@FileSystem@GAME@@QAE_NW4Partition@12@ABV" + STR + "PBD_N33@Z", OPT),
        ("GETUISCALE", "?GetUIScale@Engine@GAME@@QBEMXZ", OPT),
        # the item-background option the inventory draw reads (TQ.exe 0x10A866:
        # gEngine->GetOptions()->GetBool(0x18)) before it tints an item by its classification.
        ("GETOPTIONS", "?GetOptions@Engine@GAME@@QAEPAVOptions@2@XZ", OPT),
        ("OPTIONS_GETBOOL", "?GetBool@Options@GAME@@QBE_NW4BoolName@12@@Z", OPT),
    ]),
    # the search field's key gate: every keyboard event enters the game through
    # Display::HandleKeyEvent (thiscall, ret 4; its one caller is Engine::ProcessUserInput), and
    # ButtonEvent::GetText is the text the engine composed for a press (ToUnicode, dead keys).
    ("Input - the search field's key gate", "Engine.dll", "TQ_INPUT", [
        ("DISPLAY_HANDLEKEYEVENT", "?HandleKeyEvent@Display@GAME@@QAEXABVButtonEvent@InputDevice@2@@Z", OPT),
        ("BUTTONEVENT_GETTEXT", "?GetText@ButtonEvent@InputDevice@GAME@@QBEPBGXZ", OPT),
    ]),
    ("GameInfo", "Engine.dll", "TQ_GAMEINFO", [
        ("GETISMULTIPLAYER", "?GetIsMultiPlayer@GameInfo@GAME@@QBE_NXZ", OPT),
        ("GETMODNAME", "?GetModName@GameInfo@GAME@@QBE?BV" + STR + "_N@Z", OPT),
    ]),
    ("GraphicsEngine / GraphicsCanvas / RenderDevice - the canvas set, optional",
     "Engine.dll", "TQ_GFX", [
        ("GETCANVAS", "?GetCanvas@GraphicsEngine@GAME@@QAEAAVGraphicsCanvas@2@XZ", OPT),
        # the Transfer page draw (TQ.exe 0xC31A0) places the page through 0x176750, which
        # scales by the UI scale unless the engine is DOWNSIZING; the frame is read from the draw
        # only when this says false.
        ("ISDOWNSIZING", "?IsDownsizing@GraphicsEngine@GAME@@QBE_NXZ", OPT),
        ("LOADTEXTURE", "?LoadTexture@GraphicsEngine@GAME@@QAEPBVGraphicsTexture@2@ABV" + STR + "@Z",
         OPT),
        ("CANVAS_GETWIDTH", "?GetWidth@GraphicsCanvas@GAME@@QBEHXZ", OPT),
        ("CANVAS_GETHEIGHT", "?GetHeight@GraphicsCanvas@GAME@@QBEHXZ", OPT),
        ("CANVAS_RENDERRECT", "?RenderRect@GraphicsCanvas@GAME@@QAEXABVRect@2@ABVColor@2@"
         "PBVGraphicsShader2@2@ABVName@2@@Z", OPT),
        # the TEXTURED overload (rect, texRect in the texture's own pixels, texture, colour,
        # shader, name) - the one the item border (TQ.exe 0x10AA60) and the icon (0x10ADE0) use; it
        # draws the equipment window's slot art. GraphicsTexture::GetWidth / GetHeight scale the
        # crop when the texture is loaded at another size than the file's.
        ("CANVAS_RENDERRECT_TEX", "?RenderRect@GraphicsCanvas@GAME@@QAEXABVRect@2@0PBVGraphicsTexture@2@"
         "ABVColor@2@PBVGraphicsShader2@2@ABVName@2@@Z", OPT),
        ("TEXTURE_GETWIDTH", "?GetWidth@GraphicsTexture@GAME@@QBEHXZ", OPT),
        ("TEXTURE_GETHEIGHT", "?GetHeight@GraphicsTexture@GAME@@QBEHXZ", OPT),
        # a gray icon the next page does not show is given back (within a world)
        ("UNLOADTEXTURE", "?UnloadTexture@GraphicsEngine@GAME@@QAEXPBVGraphicsTexture@2@@Z", OPT),
        ("CANVAS_RENDERTEXT_A", "?RenderText@GraphicsCanvas@GAME@@QAEMHHABVColor@2@PBDPBVGraphicsFont@2@"
         "HW4XAlignment@12@W4YAlignment@12@_NHW4RenderFontStyle@2@5@Z", OPT),
        ("CANVAS_RENDERTEXT_W", "?RenderText@GraphicsCanvas@GAME@@QAEMHHABVColor@2@PBGPBVGraphicsFont@2@"
         "HW4XAlignment@12@W4YAlignment@12@_NHW4RenderFontStyle@2@5@Z", OPT),
        ("RD_GETACTIVEAPI", "?GetActiveAPI@RenderDevice@GAME@@SA?AW4RenderApiId@2@XZ", OPT),
        # the pad's caption font by NAME. LoadFont(name, bool override, bool flag) - the `1`
        # in the mangling back-references _N; the engine's own callers pass (name, true, false).
        ("LOADFONT", "?LoadFont@GraphicsEngine@GAME@@QAEPBVGraphicsFont@2@ABV" + STR + "_N1@Z", OPT),
    ]),
    ("Object lookup and records", "Engine.dll", "TQ_OBJ", [
        ("OM_GET", "?Get@?$Singleton@VObjectManager@GAME@@@GAME@@SAPAVObjectManager@2@XZ", OPT),
        ("OM_DESTROYOBJECTEX", "?DestroyObjectEx@ObjectManager@GAME@@QAEXPAVObject@2@PBDH@Z", OPT),
        ("GETOBJECTID", "?GetObjectId@Object@GAME@@QBEIXZ", OPT),
        ("GETOBJECTNAME", "?GetObjectName@Object@GAME@@QBEPBDXZ", OPT),
        ("LTB_GETINT", "?GetInt@LoadTableBinary@GAME@@UBEHPBDH@Z", OPT),
        # the owned walk - every object once, into a vector the ENGINE allocates (freed
        # through MSVCR110 operator delete).
        ("OM_GETOBJECTLIST", "?GetObjectList@ObjectManager@GAME@@QBEXAAV?$vector@PBVObject@GAME@@"
         "V?$allocator@PBVObject@GAME@@@std@@@std@@@Z", OPT),
    ]),
    ("GameEngine - the frame, the three stores, the caravan", "Game.dll", "TQ_GAMEENGINE", [
        ("GGAMEENGINE", "?gGameEngine@GAME@@3PAVGameEngine@1@A", REQ),
        ("UPDATE", "?Update@GameEngine@GAME@@QAEXH@Z", REQ),
        ("GETMAINPLAYER", "?GetMainPlayer@GameEngine@GAME@@QBEPAVPlayer@2@XZ", REQ),
        # the inventory draw's item tint (TQ.exe 0x10A850): the colour per classification
        # and the background opacity (hover flag), both const reads of the game engine.
        ("GETITEMCOLOR", "?GetItemColor@GameEngine@GAME@@QBE_NW4ItemClassification@2@AAVColor@2@@Z",
         OPT),
        ("GETITEMBACKGROUNDOPACITY", "?GetItemBackgroundOpacity@GameEngine@GAME@@QBEM_N@Z", OPT),
        ("GETPLAYERSTASH", "?GetPlayerStash@GameEngine@GAME@@QAEAAVInventorySack@2@XZ", REQ),
        ("GETPLAYERSTASH_C", "?GetPlayerStash@GameEngine@GAME@@QBEABVInventorySack@2@XZ", REQ),
        ("GETPLAYERTRANSFER", "?GetPlayerTransfer@GameEngine@GAME@@QAEAAVInventorySack@2@XZ", REQ),
        ("GETPLAYERTRANSFER_C", "?GetPlayerTransfer@GameEngine@GAME@@QBEABVInventorySack@2@XZ", REQ),
        ("GETPLAYERRELICVAULT", "?GetPlayerRelicVault@GameEngine@GAME@@QAEAAVInventorySack@2@XZ", REQ),
        ("GETPLAYERRELICVAULT_C", "?GetPlayerRelicVault@GameEngine@GAME@@QBEABVInventorySack@2@XZ",
         REQ),
        ("GETCARAVANMODE", "?GetCaravanMode@GameEngine@GAME@@QBEHXZ", REQ),
        ("SETCARAVANMODE", "?SetCaravanMode@GameEngine@GAME@@QAEXH@Z", REQ),
        ("CARAVANGOODBYE", "?CaravanGoodbye@GameEngine@GAME@@QAEXXZ", REQ),
        # The id overloads only: the Vec2 ones are the FILE LOAD path and are never hooked.
        ("ADDITEMTOSTASH_ID", "?AddItemToStash@GameEngine@GAME@@QAE_NI_N@Z", REQ),
        ("ADDITEMTOTRANSFER_ID", "?AddItemToTransfer@GameEngine@GAME@@QAE_NI_N@Z", REQ),
        ("ADDITEMTORELICVAULT_ID", "?AdditemToRelicVault@GameEngine@GAME@@QAE_NI_N@Z", REQ),
        ("REMOVEITEMFROMSTASH", "?RemoveItemFromStash@GameEngine@GAME@@QAE_NI@Z", REQ),
        ("REMOVEITEMFROMTRANSFER", "?RemoveItemFromTransfer@GameEngine@GAME@@QAE_NI@Z", REQ),
        ("REMOVEITEMFROMRELICVAULT", "?RemoveItemFromRelicVault@GameEngine@GAME@@QAE_NI@Z", REQ),
    ]),
    ("NpcCaravan / Player", "Game.dll", "TQ_NPC", [
        ("ONPLAYERINTERACT", "?OnPlayerInteract@NpcCaravan@GAME@@UAEXI_N0@Z", REQ),
        ("PLAYER_ISINMAINQUEST", "?IsInMainQuest@Player@GAME@@QBE_NXZ", OPT),
    ]),
    # the owned marking reads the character's inventory ids (const ref, no allocation).
    ("Character - the owned marking", "Game.dll", "TQ_CHAR", [
        ("GETINVENTORYITEMS",
         "?GetInventoryItems@Character@GAME@@QBEABV?$vector@IV?$allocator@I@std@@@std@@XZ", OPT),
    ]),
    # the drag-drop deposit disposes of the cursor original exactly as the engine's own
    # PrimaryTransferActivate does after its add (Game.dll 0x1526D3..0x1526F0):
    # ControllerCharacter::SendRemoveItemFromInventory(id) on the handler player's controller.
    ("Controller - the deposit's disposal", "Game.dll", "TQ_CTRL", [
        ("GETCONTROLLERID", "?GetControllerId@Character@GAME@@QBE?BIXZ", OPT),
        ("SENDREMOVEITEM", "?SendRemoveItemFromInventory@ControllerCharacter@GAME@@QAEXI@Z", OPT),
        ("PLAYER_VFTABLE", "??_7ControllerPlayer@GAME@@6B@", OPT),
        # the right-click take's room check - the sacks PlayerInventoryCtrl::AddItem tries
        # (Game.dll 0x21A450: its focus sack, sack 0, then every sack of its vector) asked with
        # InventorySack::IsSpaceForItem (FindNextPosition, const) before the take is journalled.
        ("PLAYER_GETINVENTORYCTRL",
         "?GetInventoryCtrl@ControllerPlayer@GAME@@QAEAAVPlayerInventoryCtrl@2@XZ", OPT),
        ("INVCTRL_GETNUMBEROFSACKS", "?GetNumberOfSacks@PlayerInventoryCtrl@GAME@@QBEIXZ", OPT),
        ("INVCTRL_GETSACK", "?GetSack@PlayerInventoryCtrl@GAME@@QAEPAVInventorySack@2@H@Z", OPT),
        # the MOUSE right-click (the page mouse handler's b2 branch, TQ.exe 0xC0208) asks
        # Player::IsInventorySpaceAvailable(item) (vt+0x2E8, through 0xC0650) BEFORE it gives the
        # item; the take asks the same function before it journals.
        ("PLAYER_ISINVENTORYSPACEAVAILABLE",
         "?IsInventorySpaceAvailable@Player@GAME@@UBE_NPBVItem@2@@Z", OPT),
        # the sound that branch plays when the room check says no (TQ.exe 0xC0650
        # -> 0xC067A); a mouse right-click take refused for room plays it too (as the Grim Dawn mod's ut_reagent.cpp does).
        ("PLAYER_PLAYINVENTORYFULLSOUND", "?PlayInventoryFullSound@Player@GAME@@QAEXXZ", OPT),
        # the inventory draw's red "requirements not met" condition (TQ.exe 0x16E3D0):
        # EquipmentCtrl::AreRequirementsMet(item) on the main player's equipment controller.
        ("CHAR_GETEQUIPMENTCTRL",
         "?GetEquipmentCtrl@ControllerCharacter@GAME@@QAEAAVEquipmentCtrl@2@XZ", OPT),
        ("EQUIPCTRL_AREREQUIREMENTSMET", "?AreRequirementsMet@EquipmentCtrl@GAME@@QBE_NPAVItem@2@@Z",
         OPT),
        # the journal's load-time reconciliation reads the EQUIPPED item ids (read only):
        # each getter is `mov eax,[ecx+d8]; ret` (Head +0x10 .. Forearm +0x64, Game.dll 0x17AC70..
        # 0x17ACE0); the two hands pick the CURRENT weapon set (+0x0D: primary +0x84/+0x98,
        # alternate +0xC0/+0xD4, Game.dll 0x17ABC0 / 0x17AC20).
        ("EQUIPCTRL_GETITEM_HEAD", "?GetItem_Head@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_NECK", "?GetItem_Neck@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_ARTIFACT", "?GetItem_Artifact@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_UPPERBODY", "?GetItem_UpperBody@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_LOWERBODY", "?GetItem_LowerBody@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_FINGER1", "?GetItem_Finger1@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_FINGER2", "?GetItem_Finger2@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_FOREARM", "?GetItem_Forearm@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_HANDLEFT", "?GetItem_HandLeft@EquipmentCtrl@GAME@@QBEIXZ", OPT),
        ("EQUIPCTRL_GETITEM_HANDRIGHT", "?GetItem_HandRight@EquipmentCtrl@GAME@@QBEIXZ", OPT),
    ]),
    ("CursorHandlerItemMove - the drop gate", "Game.dll", "TQ_CURSOR", [
        ("VFTABLE", "??_7CursorHandlerItemMove@GAME@@6B@", REQ),
        ("GETID", "?GetId@CursorHandlerItemMove@GAME@@UAEIXZ", REQ),
        ("SETID", "?SetId@CursorHandlerItemMove@GAME@@UAEXI@Z", REQ),
        ("PRIMARYSTASHACTIVATE", "?PrimaryStashActivate@CursorHandlerItemMove@GAME@@UAE_NABVVec2@2@@Z",
         REQ),
        ("PRIMARYTRANSFERACTIVATE",
         "?PrimaryTransferActivate@CursorHandlerItemMove@GAME@@UAE_NABVVec2@2@@Z", REQ),
        ("PRIMARYRELICVAULTACTIVE",
         "?PrimaryRelicVaultActive@CursorHandlerItemMove@GAME@@UAE_NABVVec2@2@@Z", REQ),
        ("ISSTASHCAPABLE", "?IsStashCapable@CursorHandlerItemMove@GAME@@UBE_NXZ", OPT),
        ("ISTRANSFERCAPABLE", "?IsTransferCapable@CursorHandlerItemMove@GAME@@UBE_NXZ", OPT),
        ("ISRELICVAULTCAPABLE", "?IsRelicVaultCapable@CursorHandlerItemMove@GAME@@UBE_NXZ", OPT),
    ]),
    ("InventorySack", "Game.dll", "TQ_SACK", [
        ("ADDITEM", "?AddItem@InventorySack@GAME@@QAE_NPAVItem@2@_N@Z", OPT),
        ("ADDITEM_VEC", "?AddItem@InventorySack@GAME@@QAE_NABVVec2@2@PAVItem@2@_N@Z", OPT),
        ("REMOVEITEM", "?RemoveItem@InventorySack@GAME@@QAE_NI@Z", OPT),
        ("ISSPACEFORITEM", "?IsSpaceForItem@InventorySack@GAME@@QBE_NPBVItem@2@@Z", OPT),
        ("GETITEMUNDERPOINT", "?GetItemUnderPoint@InventorySack@GAME@@QBEIVVec2@2@@Z", OPT),
        ("CTOR", "??0InventorySack@GAME@@QAE@XZ", OPT),
        # the mod sack and the refusals.
        ("VFTABLE", "??_7InventorySack@GAME@@6B@", OPT),
        ("SORT", "?Sort@InventorySack@GAME@@QAE_NI@Z", OPT),
        ("SETDIMS", "?SetDims@InventorySack@GAME@@QAEXHH_N@Z", OPT),
        ("CONTAINSITEM", "?ContainsItem@InventorySack@GAME@@QBE_NI@Z", OPT),
        ("GETCELLWIDTH", "?GetCellWidth@InventorySack@GAME@@QBEIXZ", OPT),
        ("GETCELLHEIGHT", "?GetCellHeight@InventorySack@GAME@@QBEIXZ", OPT),
        ("GETGRIDWIDTH", "?GetGridWidth@InventorySack@GAME@@QBEIXZ", OPT),
        ("GETGRIDHEIGHT", "?GetGridHeight@InventorySack@GAME@@QBEIXZ", OPT),
        ("GETINVENTORY", "?GetInventory@InventorySack@GAME@@QBEABV?$map@IURectExt@GAME@@U?$less@I@std@@"
         "V?$allocator@U?$pair@$$CBIURectExt@GAME@@@std@@@4@@std@@XZ", OPT),
    ]),
    ("Item", "Game.dll", "TQ_ITEM", [
        ("VFTABLE", "??_7Item@GAME@@6BActor@1@@", OPT),
        ("CREATEITEM", "?CreateItem@Item@GAME@@SAPAV12@ABUItemReplicaInfo@2@@Z", OPT),
        ("GETITEMREPLICAINFO", "?GetItemReplicaInfo@Item@GAME@@UBEXAAUItemReplicaInfo@2@@Z", OPT),
        ("GETITEMCLASSIFICATION", "?GetItemClassification@Item@GAME@@UBE?AW4ItemClassification@2@XZ",
         OPT),
        ("GETITEMTYPE", "?GetItemType@Item@GAME@@UBE?BW4Item_Type@2@XZ", OPT),
        # the classification the inventory draw tints by (TQ.exe 0x10A8AA)
        ("GETACTUALITEMCLASSIFICATION",
         "?GetActualItemClassification@Item@GAME@@QBE?AW4ItemClassification@2@XZ", OPT),
        ("GETNUMBERINSTACK", "?GetNumberInStack@Item@GAME@@UBEIXZ", OPT),
    ]),
    # the "In your collection" rollover line (GD ut_tooltip). Four of the five are detoured
    # ALL-OR-NOTHING (every weapon / armour / jewellery vftable's +0x14C slot is ItemEquipment's,
    # which calls Item's at Game.dll 0x1C00D5 and then appends); the GameTextLine ctor is CALLED,
    # never hooked. All optional: a miss turns the line off, never the mod.
    ("Item rollover - the collection tooltip line", "Game.dll", "TQ_TIP", [
        ("ITEM_GETUIDISPLAYTEXT", "?GetUIDisplayText@Item@GAME@@UBEXPBVCharacter@2@AAV" + TIPVEC + "@Z",
         OPT),
        ("ITEMEQUIPMENT_GETUIDISPLAYTEXT",
         "?GetUIDisplayText@ItemEquipment@GAME@@UBEXPBVCharacter@2@AAV" + TIPVEC + "@Z", OPT),
        ("ITEMARTIFACT_GETUIDISPLAYTEXT",
         "?GetUIDisplayText@ItemArtifact@GAME@@UBEXPBVCharacter@2@AAV" + TIPVEC + "@Z", OPT),
        ("ITEMRELIC_GETUIDISPLAYTEXT",
         "?GetUIDisplayText@ItemRelic@GAME@@UBEXPBVCharacter@2@AAV" + TIPVEC + "@Z", OPT),
        ("GAMETEXTLINETOSTRING", "?GameTextLineToString@GAME@@YAXABV" + TIPVEC + "AAV?$list@UGameTextString"
         "@GAME@@V?$allocator@UGameTextString@GAME@@@std@@@3@@Z", OPT),
        ("GAMETEXTLINE_CTOR", "??0GameTextLine@GAME@@QAE@W4GameTextClass@1@ABV?$basic_string@GU?$char_"
         "traits@G@std@@V?$allocator@G@2@@std@@_N@Z", OPT),
    ]),
    # The game's CRT. Every std::string the engine fills is freed through ITS operator delete,
    # never through the mod's own static CRT (/MT). No dump: tools/test_bindings.cpp checks it
    # against the MSVCR110.dll the game loads.
    ("MSVCR110 - the game's C runtime", "MSVCR110.dll", "TQ_CRT", [
        ("OPERATORDELETE", "??3@YAXPAX@Z", OPT),
    ]),
]

# Exports that create, move, delete or persist items: listed, but the comment says WRITE.
FORBIDDEN = re.compile(
    r"^\?(AddItem|AdditemTo|RemoveItem|DestroyObject|CreateItem|SetId|SetCaravanMode|CaravanGoodbye"
    r"|LoadDatabase|LoadMainDatabase|InitializeMod)")

CC_NAMES = {"TQCC_DATA": "data", "TQCC_THISCALL": "__thiscall", "TQCC_CDECL": "__cdecl",
            "TQCC_STDCALL": "__stdcall", "TQCC_FASTCALL": "__fastcall"}

# member function: access letter, A/B (cv), then the convention letter
_MEMBER = re.compile(r"@@([ABEFIJMNQRUV])([AB])([AEGI])")
_STATIC = re.compile(r"@@([CKS])([AGI])")
_FREE = re.compile(r"@(?:@)?Y([AGI])")
_DATA = re.compile(r"@@([0-4]|6B)")
_CCLETTER = {"A": "TQCC_CDECL", "E": "TQCC_THISCALL", "G": "TQCC_STDCALL", "I": "TQCC_FASTCALL"}


def derive(name: str):
    """(cc macro, kind, sret) from a decorated name. Raises on an unknown shape."""
    if name.startswith("??_7"):
        return "TQCC_DATA", "vftable", 0
    for m in re.finditer(r"@@", name):
        tail = name[m.start():]
        mm = _MEMBER.match(tail)
        if mm:
            rest = tail[mm.end():]
            sret = 1 if re.match(r"\?[AB][VU]", rest) else 0
            virt = mm.group(1) in "EFMNUV"
            return _CCLETTER[mm.group(3)], ("virtual member" if virt else "member"), sret
        ms = _STATIC.match(tail)
        if ms:
            rest = tail[ms.end():]
            sret = 1 if re.match(r"\?[AB][VU]", rest) else 0
            return _CCLETTER[ms.group(2)], "static member", sret
        md = _DATA.match(tail)
        if md:
            return "TQCC_DATA", ("global data" if md.group(1) == "3" else "static data"), 0
    mf = _FREE.search(name)
    if mf:
        return _CCLETTER[mf.group(1)], "free function", 0
    raise ValueError("cannot derive the calling convention of " + name)


HEADER_TOP = r'''// tq_exports.h - generated by tools/gen_exports_header.py. DO NOT EDIT BY HAND.
//
// Titan Quest Anniversary Edition 2.10, x86. Exported symbol names checked verbatim against the
// export tables of Engine.dll / Game.dll (tools/tqexports.py); the comment above each macro is
// undname.exe output for that exact string. ZERO ADDRESSES: everything is resolved by name at
// run time (GetModuleHandle + GetProcAddress on modules the game already loaded).
//
// Calling convention: x86 does NOT collapse them. Every <name>_CC below is what the MANGLING says:
//   TQCC_THISCALL  a member (QAE/QBE/UAE/UBE/AAE): `this` in ECX, callee cleans the stack. The mod
//                  calls it through a __thiscall typedef and DETOURS it as
//                  __fastcall(void* self, void* edx_unused, args...) - the same stack shape.
//   TQCC_CDECL     a free function (YA) or a static member (SA).
//   TQCC_DATA      a global, static data or a vftable - the address of the variable.
// <name>_SRET 1 = a class is returned by value: a hidden return pointer is the first stack
// argument after `this`. <name>_REQ 1 = REQUIRED: a miss turns the mod off before any hook.
//
// Names that create, move, delete or persist something are marked WRITE: hooked to observe, never
// called by the mod.

#pragma once

#define TQCC_DATA 0
#define TQCC_THISCALL 1
#define TQCC_CDECL 2
#define TQCC_STDCALL 3
#define TQCC_FASTCALL 4
'''


def undecorate(names, undname_exe, chunk=40):
    out = {}
    if not undname_exe:
        return {n: "<undname not found>" for n in names}
    for i in range(0, len(names), chunk):
        part = names[i:i + chunk]
        try:
            res = subprocess.run([undname_exe] + part, capture_output=True, text=True, timeout=120)
        except (OSError, subprocess.SubprocessError) as exc:
            for n in part:
                out[n] = f"<undname failed: {exc}>"
            continue
        cur = None
        for line in res.stdout.splitlines():
            m = re.match(r'^Undecoration of :- "(.*)"$', line.strip())
            if m:
                cur = m.group(1)
                continue
            m = re.match(r'^is :- "(.*)"$', line.strip())
            if m and cur is not None:
                out[cur] = m.group(1)
                cur = None
        for n in part:
            out.setdefault(n, "<undname produced no output>")
    return out


def load_dump(exports_dir: str, module: str):
    stem = os.path.splitext(module)[0]
    path = os.path.join(exports_dir, "exports_%s.txt" % stem)
    if not os.path.exists(path):
        return None
    names = set()
    with open(path, encoding="latin-1") as f:
        for ln in f:
            p = ln.split()
            if len(p) >= 3:
                names.add(p[2])
    return names


def render(exports_dir: str, undname_exe: str) -> str:
    dumps = {}
    rows = []
    for title, module, prefix, entries in SELECTION:
        if module not in dumps:
            dumps[module] = load_dump(exports_dir, module)
            if dumps[module] is None and module != "MSVCR110.dll":
                raise SystemExit("no export dump for %s in %s" % (module, exports_dir))
        for suffix, name, req in entries:
            d = dumps[module]
            if d is not None and name not in d:
                raise SystemExit("NOT IN THE %s DUMP: %s (%s_%s)" % (module, name, prefix, suffix))
            cc, kind, sret = derive(name)
            rows.append((title, module, prefix + "_" + suffix, name, cc, kind, sret, req))
    und = undecorate([r[3] for r in rows], undname_exe)

    lines = [HEADER_TOP]
    lines.append("#define TQ_EXPORT_COUNT %d" % len(rows))
    lines.append("#define TQ_EXPORT_REQUIRED_COUNT %d" % sum(1 for r in rows if r[7]))
    lines.append("")
    last = None
    for title, module, macro, name, cc, kind, sret, req in rows:
        if title != last:
            lines.append("// " + "-" * 93)
            lines.append(f"// {title}   [{module}]")
            lines.append("// " + "-" * 93)
            last = title
        flag = "  ** WRITE - hooked to observe, never called **" if FORBIDDEN.match(name) else ""
        extra = "  [returns by value -> hidden sret pointer]" if sret else ""
        lines.append(f"// {und.get(name, '?')}{flag}{extra}")
        lines.append(f"//   {kind}, {CC_NAMES[cc]}, {'REQUIRED' if req else 'optional'}")
        lines.append(f'#define {macro} "{name}"')
        lines.append(f'#define {macro}_MOD "{module}"')
        lines.append(f"#define {macro}_CC {cc}")
        lines.append(f"#define {macro}_SRET {sret}")
        lines.append(f"#define {macro}_REQ {req}")
        lines.append("")
    lines.append("// Every row, for the offline test: X(macro, module, decorated name, required)")
    lines.append("#define TQ_EXPORTS_FOR_EACH(X) \\")
    for i, r in enumerate(rows):
        lines.append("    X(%s, %s_MOD, %s, %s_REQ)%s" % (r[2], r[2], r[2], r[2],
                                                      " \\" if i + 1 < len(rows) else ""))
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exports", default=DEFAULT_EXPORTS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--undname", default=DEFAULT_UNDNAME)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    text = render(a.exports, a.undname)
    n = len(re.findall(r"^#define \w+_REQ [01]$", text, re.M))
    if a.check:
        cur = open(a.out, encoding="utf-8").read() if os.path.exists(a.out) else ""
        if cur.replace("\r\n", "\n") != text:
            print("tq_exports.h is STALE - rerun tools/gen_exports_header.py")
            return 1
        print(f"tq_exports.h is current ({n} exports)")
        return 0
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print(f"{os.path.relpath(a.out, ROOT)}: {n} exports, {os.path.getsize(a.out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())

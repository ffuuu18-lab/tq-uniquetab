// tq_runtime.h - the engine functions this mod resolves by name, and the raw structs it passes.
//
// Every mangled name comes from src/tq_exports.h (generated from the real export tables);
// nothing here is typed by hand. Titan Quest is x86, where the conventions do NOT collapse the way
// they do on x64: every member is __thiscall (`this` in ECX, callee cleans), a free function or a
// static member is __cdecl. Each typedef below is checked against what the MANGLING says
// (TQ_EXPECT_CC), so a typedef that disagrees with the export does not compile. A detour of a
// __thiscall target is written __fastcall(self, void* edx_unused, args...) - the same stack shape.
#pragma once

#include <windows.h>

#include "tq_exports.h"

#define TQ_EXPECT_CC(x, cc) \
    static_assert(x##_CC == (cc), "the calling convention of " #x " differs from its mangling")

// Opaque engine handles - never dereferenced except where a comment says so.
struct TqEngine;
struct TqGameInfo;
struct TqGraphicsEngine;
struct TqCanvas;
struct TqTexture;        // GAME::GraphicsTexture
struct TqGameEngine;
struct TqPlayer;         // GAME::Player
struct TqItem;           // GAME::Item  (derives from GAME::Object)
struct TqSack;           // GAME::InventorySack
struct TqCursorItemMove; // GAME::CursorHandlerItemMove
struct TqNpcCaravan;     // GAME::NpcCaravan

// GAME::Rect / Color / Vec2 - floats, verified live in the game.
struct TqRect {
    float x, y, w, h;
};
struct TqColor {
    float r, g, b, a;
};
struct TqVec2 {
    float x, y;
};

// A bool argument as the 4-byte stack slot it occupies: a detour forwards the caller's exact bits
// (the callee reads the low byte) instead of a re-normalised C++ bool.
typedef unsigned int Bool32;

// The game's std::string: VS2012 x86 (MSVCR110/MSVCP110). 16-byte buffer/pointer union, then the
// size, then the capacity; capacity < 16 means the text is in place. STRUCTURAL - see ut_bindings.h.
struct TqStdString {
    union {
        char buf[16];
        char* ptr;
    };
    unsigned size;
    unsigned res;
};
static_assert(sizeof(TqStdString) == 0x18, "VS2012 x86 std::string is 0x18 bytes");

// ---- function pointer types -----------------------------------------------------------------
typedef void(__thiscall* PfnEngine_Void)(TqEngine*);  // PresentSurface, LoadMainDatabase
typedef bool(__thiscall* PfnEngine_LoadDatabase)(TqEngine*, const TqStdString* path);
typedef unsigned(__thiscall* PfnEngine_GetChecksum)(TqEngine*);
typedef bool(__thiscall* PfnEngine_Bool)(TqEngine*);  // HasLoadedCustomDatabase, IsNetwork*
typedef bool(__thiscall* PfnEngine_InitializeMod)(TqEngine*, const TqStdString*, const TqStdString*);
typedef TqGameInfo*(__thiscall* PfnEngine_GetGameInfo)(TqEngine*);
typedef TqGraphicsEngine*(__thiscall* PfnEngine_GetGraphicsEngine)(const TqEngine*);
typedef float(__thiscall* PfnEngine_GetUIScale)(const TqEngine*);
TQ_EXPECT_CC(TQ_ENGINE_PRESENTSURFACE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_LOADMAINDATABASE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_LOADDATABASE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_GETDATABASEARCHIVECHECKSUM, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_HASLOADEDCUSTOMDATABASE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_ISNETWORKCLIENT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_ISNETWORKSERVER, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_INITIALIZEMOD, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_GETGAMEINFO, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_GETGRAPHICSENGINE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_GETUISCALE, TQCC_THISCALL);

typedef bool(__thiscall* PfnGameInfo_GetIsMultiPlayer)(const TqGameInfo*);
// Returns std::string BY VALUE: the hidden return pointer is the first stack argument. The string
// the engine fills is freed through MSVCR110 operator delete, never through this DLL's own CRT.
typedef TqStdString*(__thiscall* PfnGameInfo_GetModName)(const TqGameInfo*, TqStdString* sret,
                                                         Bool32);
TQ_EXPECT_CC(TQ_GAMEINFO_GETISMULTIPLAYER, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEINFO_GETMODNAME, TQCC_THISCALL);
static_assert(TQ_GAMEINFO_GETMODNAME_SRET == 1, "GetModName returns std::string by value");

typedef TqCanvas*(__thiscall* PfnGfx_GetCanvas)(TqGraphicsEngine*);  // returns a reference
typedef bool(__thiscall* PfnGfx_IsDownsizing)(const TqGraphicsEngine*);
typedef const TqTexture*(__thiscall* PfnGfx_LoadTexture)(TqGraphicsEngine*, const TqStdString*);
typedef int(__thiscall* PfnCanvas_GetInt)(const TqCanvas*);           // GetWidth / GetHeight
typedef void(__thiscall* PfnCanvas_RenderRect)(TqCanvas*, const TqRect*, const TqColor*,
                                               const void* shader, const void* name);
// the TEXTURED overload (TQ.exe's item border 0x10AA60 and icon 0x10ADE0 call it): texRect is
// in the texture's own pixels (the icon passes 0, 0, GetWidth, GetHeight).
typedef void(__thiscall* PfnCanvas_RenderRectTex)(TqCanvas*, const TqRect* rect, const TqRect* texRect,
                                                  const TqTexture* tex, const TqColor*,
                                                  const void* shader, const void* name);
typedef int(__thiscall* PfnTexture_GetInt)(const TqTexture*);   // GraphicsTexture::GetWidth / GetHeight
// the gray icons. Engine::GetFileSystem; FileSystem::AddSource(partition, dir, char const*,
// bool, bool, bool) - TQ.exe passes (1, "./Settings/", 0, 0, 0, 0) for a loose-file directory (the
// first bool = scan the folder for *.arc instead); GraphicsEngine::UnloadTexture.
typedef void*(__thiscall* PfnEngine_GetFileSystem)(TqEngine*);
typedef bool(__thiscall* PfnFs_AddSource)(void* fs, int partition, const TqStdString* dir,
                                          const char* prefix, Bool32, Bool32, Bool32);
typedef void(__thiscall* PfnGfx_UnloadTexture)(TqGraphicsEngine*, const TqTexture*);
typedef float(__thiscall* PfnCanvas_RenderTextA)(TqCanvas*, int x, int y, const TqColor*,
                                                 const char*, const void* font, int size, int xa,
                                                 int ya, Bool32, int, int, int);
typedef float(__thiscall* PfnCanvas_RenderTextW)(TqCanvas*, int x, int y, const TqColor*,
                                                 const wchar_t*, const void* font, int size, int xa,
                                                 int ya, Bool32, int, int, int);
typedef int(__cdecl* PfnRenderDevice_GetActiveAPI)();
// Display::HandleKeyEvent(ButtonEvent const&) - the key gate's target (detoured, never called by
// the mod except through its trampoline); ButtonEvent::GetText() const -> the typed text.
typedef void(__thiscall* PfnDisplay_HandleKeyEvent)(void* display, const void* buttonEvent);
typedef const unsigned short*(__thiscall* PfnButtonEvent_GetText)(const void* buttonEvent);
// LoadFont(const std::string& name, bool override, bool flag): the engine's own callers pass
// (name, true, false); an unknown name faults inside (it writes the flag into a null font).
typedef const void*(__thiscall* PfnGfx_LoadFont)(TqGraphicsEngine*, const TqStdString*, Bool32, Bool32);
TQ_EXPECT_CC(TQ_GFX_LOADFONT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_GETCANVAS, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_ISDOWNSIZING, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_LOADTEXTURE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_CANVAS_GETWIDTH, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_CANVAS_GETHEIGHT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_CANVAS_RENDERRECT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_CANVAS_RENDERRECT_TEX, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_TEXTURE_GETWIDTH, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_TEXTURE_GETHEIGHT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_UNLOADTEXTURE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_GETFILESYSTEM, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_FS_ADDSOURCE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_CANVAS_RENDERTEXT_A, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_CANVAS_RENDERTEXT_W, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GFX_RD_GETACTIVEAPI, TQCC_CDECL);

// GAME::Object accessors. Item derives from Object, so an Item* is a valid `this` here.
typedef void*(__cdecl* PfnObjectManager_Get)();
typedef void(__thiscall* PfnObjectManager_DestroyObjectEx)(void* om, void* obj, const char* file,
                                                           int line);
typedef unsigned(__thiscall* PfnObject_GetObjectId)(const void*);
typedef const char*(__thiscall* PfnObject_GetObjectName)(const void*);
typedef int(__thiscall* PfnLoadTable_GetInt)(const void* table, const char* key, int def);
// A VS2012 x86 std::vector of pointers: first / last / end (STRUCTURAL, 12 bytes). The engine
// fills it through ITS allocator, so the buffer is freed through MSVCR110 operator delete.
struct TqPtrVector {
    const void** first;
    const void** last;
    const void** end;
};
static_assert(sizeof(TqPtrVector) == 12, "VS2012 x86 std::vector is 12 bytes");
typedef void(__thiscall* PfnObjectManager_GetObjectList)(const void* om, TqPtrVector* out);
TQ_EXPECT_CC(TQ_OBJ_OM_GETOBJECTLIST, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_OBJ_OM_GET, TQCC_CDECL);
TQ_EXPECT_CC(TQ_OBJ_OM_DESTROYOBJECTEX, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_OBJ_GETOBJECTID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_OBJ_GETOBJECTNAME, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_OBJ_LTB_GETINT, TQCC_THISCALL);

// GAME::GameEngine
typedef void(__thiscall* PfnGameEngine_Update)(TqGameEngine*, int deltaMs);
typedef TqPlayer*(__thiscall* PfnGameEngine_GetMainPlayer)(const TqGameEngine*);
// The six page getters: `lea eax,[ecx+d32]; ret`, one per store and constness.
typedef TqSack*(__thiscall* PfnGameEngine_GetSack)(TqGameEngine*);
typedef int(__thiscall* PfnGameEngine_GetCaravanMode)(const TqGameEngine*);
typedef void(__thiscall* PfnGameEngine_SetCaravanMode)(TqGameEngine*, int mode);
typedef void(__thiscall* PfnGameEngine_Void)(TqGameEngine*);  // CaravanGoodbye
typedef bool(__thiscall* PfnGameEngine_AddItemId)(TqGameEngine*, unsigned itemId, Bool32);
typedef bool(__thiscall* PfnGameEngine_RemoveItem)(TqGameEngine*, unsigned itemId);
TQ_EXPECT_CC(TQ_GAMEENGINE_UPDATE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETMAINPLAYER, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETPLAYERSTASH, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETPLAYERSTASH_C, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETPLAYERTRANSFER, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETPLAYERTRANSFER_C, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETPLAYERRELICVAULT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETPLAYERRELICVAULT_C, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETCARAVANMODE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_SETCARAVANMODE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_CARAVANGOODBYE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_ADDITEMTOSTASH_ID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_ADDITEMTOTRANSFER_ID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_ADDITEMTORELICVAULT_ID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_REMOVEITEMFROMSTASH, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_REMOVEITEMFROMTRANSFER, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_REMOVEITEMFROMRELICVAULT, TQCC_THISCALL);

typedef void(__thiscall* PfnNpcCaravan_OnPlayerInteract)(TqNpcCaravan*, unsigned id, Bool32, Bool32);
typedef bool(__thiscall* PfnPlayer_Bool)(const TqPlayer*);
TQ_EXPECT_CC(TQ_NPC_ONPLAYERINTERACT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_NPC_PLAYER_ISINMAINQUEST, TQCC_THISCALL);

// GAME::CursorHandlerItemMove - the drop gate. OBSERVED ONLY.
typedef unsigned(__thiscall* PfnCursor_GetId)(TqCursorItemMove*);
typedef void(__thiscall* PfnCursor_SetId)(TqCursorItemMove*, unsigned itemId);
typedef bool(__thiscall* PfnCursor_Primary)(TqCursorItemMove*, const TqVec2* pos);
typedef bool(__thiscall* PfnCursor_Capable)(const TqCursorItemMove*);
TQ_EXPECT_CC(TQ_CURSOR_VFTABLE, TQCC_DATA);
TQ_EXPECT_CC(TQ_CURSOR_GETID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_SETID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_PRIMARYSTASHACTIVATE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_PRIMARYTRANSFERACTIVATE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_PRIMARYRELICVAULTACTIVE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_ISSTASHCAPABLE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_ISTRANSFERCAPABLE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CURSOR_ISRELICVAULTCAPABLE, TQCC_THISCALL);

// GAME::InventorySack. Resolved, never hooked and never called.
typedef bool(__thiscall* PfnSack_AddItem)(TqSack*, TqItem*, Bool32);
// the Bool32 of both AddItem forms is "silent": when it is false the sack calls the item's
// PlayDropSound (vtable +0x130) after the insert; the mod places its prototypes with it set.
typedef bool(__thiscall* PfnSack_AddItemVec)(TqSack*, const TqVec2*, TqItem*, Bool32);
typedef bool(__thiscall* PfnSack_RemoveItem)(TqSack*, unsigned itemId);
typedef bool(__thiscall* PfnSack_IsSpaceForItem)(const TqSack*, const TqItem*);
// GetItemUnderPoint(Vec2 by value): on x86 the two floats are the first 8 bytes of the stack.
typedef unsigned(__thiscall* PfnSack_GetItemUnderPoint)(const TqSack*, float x, float y);
typedef TqSack*(__thiscall* PfnSack_Ctor)(void* self);
typedef bool(__thiscall* PfnSack_Sort)(TqSack*, unsigned arg);
typedef void(__thiscall* PfnSack_SetDims)(TqSack*, int pixelW, int pixelH, Bool32);
typedef bool(__thiscall* PfnSack_ContainsItem)(const TqSack*, unsigned itemId);
typedef unsigned(__thiscall* PfnSack_GetU)(const TqSack*);  // cell / grid width, height
// Returns const std::map<unsigned, RectExt>& - the map object itself (STRUCTURAL: head, size).
typedef const void*(__thiscall* PfnSack_GetInventory)(const TqSack*);
TQ_EXPECT_CC(TQ_SACK_VFTABLE, TQCC_DATA);
TQ_EXPECT_CC(TQ_SACK_SORT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_SETDIMS, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_CONTAINSITEM, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_GETCELLWIDTH, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_GETCELLHEIGHT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_GETGRIDWIDTH, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_GETGRIDHEIGHT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_GETINVENTORY, TQCC_THISCALL);
// Character::GetInventoryItems() const -> const std::vector<unsigned>& (the character's item
// ids; the vector is the engine's, read only). Called on the main Player (Player : Character).
struct TqU32Vector {
    const unsigned* first;
    const unsigned* last;
    const unsigned* end;
};
static_assert(sizeof(TqU32Vector) == 12, "VS2012 x86 std::vector is 12 bytes");
typedef const TqU32Vector*(__thiscall* PfnCharacter_GetInventoryItems)(const void* character);
TQ_EXPECT_CC(TQ_CHAR_GETINVENTORYITEMS, TQCC_THISCALL);

// TQ.exe-internal, located by the counted signatures: the Transfer page accessor
// (sub-window slot +0x20, one stack argument forwarded to the children, `ret 4`) and
// UIStashInventory::StreamOut(const std::string&) (`ret 4`). Both return whatever EAX holds.
typedef unsigned(__thiscall* PfnExe_PageAccessor)(void* subWindow, unsigned arg);
typedef unsigned(__thiscall* PfnExe_StreamOut)(void* ui, const TqStdString* path);
// the Transfer page window's draw (TQ.exe 0xC31A0, vtable 0x21D1C0 slot +0x0C, `ret 0x10`):
// (canvas, parent origin, pass, alpha); it draws the inventory at this+0x88 through [vft+0x0C].
typedef void(__thiscall* PfnExe_PageDraw)(void* page, void* canvas, const TqVec2* origin, int pass,
                                          float alpha);
// an item widget's background (TQ.exe 0x10A9B0, vtable slot +0x88 of the four item-widget
// classes, `ret 0x18`): (canvas, origin, colour, inset, met, 1). It reads the widget's rect
// [+0x10..+0x1C] (x, y, w, h) and scale [+0x78] and calls [vft+0x84] = 0x10A850 (the tint, then the
// border texture [widget+0x74] over the SAME rect). The icon (vt+0x0C) reads the same rect after it.
typedef void(__thiscall* PfnExe_ItemBackground)(void* widget, void* canvas, const TqVec2* origin,
                                                const TqColor* colour, float inset, Bool32 met,
                                                Bool32 one);
TQ_EXPECT_CC(TQ_SACK_ADDITEM, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_ADDITEM_VEC, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_REMOVEITEM, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_ISSPACEFORITEM, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_GETITEMUNDERPOINT, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_SACK_CTOR, TQCC_THISCALL);

// GAME::Item. GetItemClassification / GetItemType return a scalar enum (EAX, no sret).
typedef TqItem*(__cdecl* PfnItem_CreateItem)(const void* replicaInfo);
typedef void(__thiscall* PfnItem_GetItemReplicaInfo)(const TqItem*, void* replicaInfo);
typedef int(__thiscall* PfnItem_GetInt)(const TqItem*);  // classification / type
typedef unsigned(__thiscall* PfnItem_GetNumberInStack)(const TqItem*);
TQ_EXPECT_CC(TQ_ITEM_VFTABLE, TQCC_DATA);
TQ_EXPECT_CC(TQ_ITEM_CREATEITEM, TQCC_CDECL);
TQ_EXPECT_CC(TQ_ITEM_GETITEMREPLICAINFO, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ITEM_GETITEMCLASSIFICATION, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ITEM_GETITEMTYPE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ITEM_GETNUMBERINSTACK, TQCC_THISCALL);

// the drag-drop deposit's disposal: the handler player's controller id,
// the controller object (a ControllerPlayer, by vftable), and its queued inventory removal.
typedef unsigned(__thiscall* PfnCharacter_GetControllerId)(const void* character);
typedef void(__thiscall* PfnCtrl_SendRemoveItem)(void* controller, unsigned itemId);
TQ_EXPECT_CC(TQ_CTRL_GETCONTROLLERID, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_SENDREMOVEITEM, TQCC_THISCALL);
// the right-click take's room check (ControllerPlayer+0x360 is the PlayerInventoryCtrl;
// GetSack does not bound its index - the caller keeps i < GetNumberOfSacks).
typedef void*(__thiscall* PfnCtrl_GetInventoryCtrl)(void* controller);
typedef unsigned(__thiscall* PfnInvCtrl_GetNumberOfSacks)(const void* invCtrl);
typedef TqSack*(__thiscall* PfnInvCtrl_GetSack)(void* invCtrl, int i);
// the mouse right-click's own room check (TQ.exe 0xC0650 -> Player vt+0x2E8), const
typedef bool(__thiscall* PfnPlayer_IsInventorySpaceAvailable)(const TqPlayer* player,
                                                               const TqItem* item);
// the engine's "inventory full" sound (0xC0650 plays it when that check says no)
typedef void(__thiscall* PfnPlayer_PlayInventoryFullSound)(TqPlayer* player);
TQ_EXPECT_CC(TQ_CTRL_PLAYER_GETINVENTORYCTRL, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_INVCTRL_GETNUMBEROFSACKS, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_PLAYER_ISINVENTORYSPACEAVAILABLE, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_PLAYER_PLAYINVENTORYFULLSOUND, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_INVCTRL_GETSACK, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_PLAYER_VFTABLE, TQCC_DATA);
// the inventory draw's item tint (TQ.exe 0x16E300 -> 0x10A9B0 -> 0x10A850), asked on the
// mod's own prototypes. TqColorF is GAME::Color (r, g, b, a floats), filled by GetItemColor.
struct TqColorF {
    float r, g, b, a;
};
typedef void*(__thiscall* PfnCtrl_GetEquipmentCtrl)(void* controller);
typedef bool(__thiscall* PfnEquipCtrl_AreRequirementsMet)(const void* equipCtrl, const void* item);
// EquipmentCtrl::GetItem_<slot>() const -> the equipped item's id (0 = empty), read only.
typedef unsigned(__thiscall* PfnEquipCtrl_GetItem)(const void* equipCtrl);
typedef int(__thiscall* PfnItem_GetActualItemClassification)(const void* item);
typedef bool(__thiscall* PfnGameEngine_GetItemColor)(const void* ge, int cls, TqColorF* out);
typedef float(__thiscall* PfnGameEngine_GetItemBackgroundOpacity)(const void* ge, bool hover);
typedef void*(__thiscall* PfnEngine_GetOptions)(void* engine);
typedef bool(__thiscall* PfnOptions_GetBool)(const void* options, int name);
TQ_EXPECT_CC(TQ_CTRL_CHAR_GETEQUIPMENTCTRL, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_CTRL_EQUIPCTRL_AREREQUIREMENTSMET, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ITEM_GETACTUALITEMCLASSIFICATION, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETITEMCOLOR, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_GAMEENGINE_GETITEMBACKGROUNDOPACITY, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_GETOPTIONS, TQCC_THISCALL);
TQ_EXPECT_CC(TQ_ENGINE_OPTIONS_GETBOOL, TQCC_THISCALL);

typedef void(__cdecl* PfnCrt_OperatorDelete)(void*);
TQ_EXPECT_CC(TQ_CRT_OPERATORDELETE, TQCC_CDECL);

// ---- the resolved set ------------------------------------------------------------------------
struct TqRuntime {
    HMODULE exe = nullptr;
    HMODULE engineDll = nullptr;
    HMODULE gameDll = nullptr;
    HMODULE crtDll = nullptr;  // MSVCR110.dll - looked up, never loaded

    TqEngine** ppEngine = nullptr;          // address of the exported GAME::gEngine variable
    TqGameEngine** ppGameEngine = nullptr;  // address of the exported GAME::gGameEngine variable

    PfnEngine_Void EnginePresentSurface = nullptr;          // hook
    PfnEngine_Void EngineLoadMainDatabase = nullptr;        // hook
    PfnEngine_LoadDatabase EngineLoadDatabase = nullptr;    // hook
    PfnEngine_GetChecksum EngineGetDatabaseArchiveChecksum = nullptr;
    PfnEngine_Bool EngineHasLoadedCustomDatabase = nullptr;
    PfnEngine_InitializeMod EngineInitializeMod = nullptr;
    PfnEngine_GetGameInfo EngineGetGameInfo = nullptr;
    PfnEngine_Bool EngineIsNetworkClient = nullptr;
    PfnEngine_Bool EngineIsNetworkServer = nullptr;
    PfnEngine_GetGraphicsEngine EngineGetGraphicsEngine = nullptr;
    PfnEngine_GetUIScale EngineGetUIScale = nullptr;
    PfnGameInfo_GetIsMultiPlayer GameInfoGetIsMultiPlayer = nullptr;
    PfnGameInfo_GetModName GameInfoGetModName = nullptr;

    PfnGfx_GetCanvas GfxGetCanvas = nullptr;
    PfnGfx_IsDownsizing GfxIsDownsizing = nullptr;   // the page-draw frame
    PfnGfx_LoadTexture GfxLoadTexture = nullptr;
    PfnCanvas_GetInt CanvasGetWidth = nullptr;
    PfnCanvas_GetInt CanvasGetHeight = nullptr;
    PfnCanvas_RenderRect CanvasRenderRect = nullptr;
    PfnCanvas_RenderRectTex CanvasRenderRectTex = nullptr;   // the slot art
    PfnTexture_GetInt TextureGetWidth = nullptr;
    PfnTexture_GetInt TextureGetHeight = nullptr;
    PfnGfx_UnloadTexture GfxUnloadTexture = nullptr;         // the gray icons
    PfnEngine_GetFileSystem EngineGetFileSystem = nullptr;
    PfnFs_AddSource FsAddSource = nullptr;
    PfnCanvas_RenderTextA CanvasRenderTextA = nullptr;
    PfnCanvas_RenderTextW CanvasRenderTextW = nullptr;
    PfnDisplay_HandleKeyEvent DisplayHandleKeyEvent = nullptr;
    PfnButtonEvent_GetText ButtonEventGetText = nullptr;
    PfnRenderDevice_GetActiveAPI RenderDeviceGetActiveAPI = nullptr;
    PfnGfx_LoadFont GfxLoadFont = nullptr;

    PfnObjectManager_Get ObjectManagerGet = nullptr;
    PfnObjectManager_DestroyObjectEx ObjectManagerDestroyObjectEx = nullptr;
    PfnObject_GetObjectId ObjectGetObjectId = nullptr;
    PfnObject_GetObjectName ObjectGetObjectName = nullptr;
    PfnLoadTable_GetInt LoadTableBinaryGetInt = nullptr;
    PfnObjectManager_GetObjectList ObjectManagerGetObjectList = nullptr;

    PfnGameEngine_Update GameUpdate = nullptr;               // hook
    PfnGameEngine_GetMainPlayer GameGetMainPlayer = nullptr;
    PfnGameEngine_GetSack GameGetPlayerStash = nullptr;      // hook (counted)
    PfnGameEngine_GetSack GameGetPlayerStashC = nullptr;     // hook (counted)
    PfnGameEngine_GetSack GameGetPlayerTransfer = nullptr;   // hook (counted)
    PfnGameEngine_GetSack GameGetPlayerTransferC = nullptr;  // hook (counted)
    PfnGameEngine_GetSack GameGetPlayerRelicVault = nullptr;   // hook (counted)
    PfnGameEngine_GetSack GameGetPlayerRelicVaultC = nullptr;  // hook (counted)
    PfnGameEngine_GetCaravanMode GameGetCaravanMode = nullptr;
    PfnGameEngine_SetCaravanMode GameSetCaravanMode = nullptr;  // hook (counted)
    PfnGameEngine_Void GameCaravanGoodbye = nullptr;            // hook
    PfnGameEngine_AddItemId GameAddItemToStashId = nullptr;     // hook
    PfnGameEngine_AddItemId GameAddItemToTransferId = nullptr;  // hook
    PfnGameEngine_AddItemId GameAddItemToRelicVaultId = nullptr;  // hook
    PfnGameEngine_RemoveItem GameRemoveItemFromStash = nullptr;     // hook
    PfnGameEngine_RemoveItem GameRemoveItemFromTransfer = nullptr;  // hook
    PfnGameEngine_RemoveItem GameRemoveItemFromRelicVault = nullptr;  // hook

    PfnNpcCaravan_OnPlayerInteract NpcCaravanOnPlayerInteract = nullptr;  // hook
    PfnPlayer_Bool PlayerIsInMainQuest = nullptr;

    const void* CursorVftable = nullptr;
    PfnCursor_GetId CursorGetId = nullptr;
    PfnCursor_SetId CursorSetId = nullptr;                          // hook
    PfnCursor_Primary CursorPrimaryStashActivate = nullptr;         // hook
    PfnCursor_Primary CursorPrimaryTransferActivate = nullptr;      // hook
    PfnCursor_Primary CursorPrimaryRelicVaultActive = nullptr;      // hook
    PfnCursor_Capable CursorIsStashCapable = nullptr;
    PfnCursor_Capable CursorIsTransferCapable = nullptr;
    PfnCursor_Capable CursorIsRelicVaultCapable = nullptr;

    PfnSack_AddItem SackAddItem = nullptr;
    PfnSack_AddItemVec SackAddItemVec = nullptr;
    PfnSack_RemoveItem SackRemoveItem = nullptr;
    PfnSack_IsSpaceForItem SackIsSpaceForItem = nullptr;
    PfnSack_GetItemUnderPoint SackGetItemUnderPoint = nullptr;
    PfnSack_Ctor SackCtor = nullptr;
    const void* SackVftable = nullptr;
    PfnSack_Sort SackSort = nullptr;  // hook (refused on the mod sack)
    PfnSack_SetDims SackSetDims = nullptr;
    PfnSack_ContainsItem SackContainsItem = nullptr;
    PfnSack_GetU SackGetCellWidth = nullptr;
    PfnSack_GetU SackGetCellHeight = nullptr;
    PfnSack_GetU SackGetGridWidth = nullptr;
    PfnSack_GetU SackGetGridHeight = nullptr;
    PfnSack_GetInventory SackGetInventory = nullptr;
    PfnCharacter_GetInventoryItems CharGetInventoryItems = nullptr;   //, advisory
    PfnCharacter_GetControllerId CharGetControllerId = nullptr;       //, advisory
    PfnCtrl_SendRemoveItem CtrlSendRemoveItem = nullptr;              //, advisory
    const void* ControllerPlayerVftable = nullptr;                    //, advisory
    PfnCtrl_GetInventoryCtrl CtrlGetInventoryCtrl = nullptr;          // the room check
    // the slot-wide item tint (the inventory draw's own calls, on the prototypes)
    PfnCtrl_GetEquipmentCtrl CtrlGetEquipmentCtrl = nullptr;
    PfnEquipCtrl_AreRequirementsMet EquipAreRequirementsMet = nullptr;
    // the ten slot getters, kUtEquipSlots order (ut_recon.cpp): head, neck, artifact, upper,
    // lower, finger1, finger2, forearm, left hand, right hand. Optional (missing = unknown).
    PfnEquipCtrl_GetItem EquipGetItem[10] = {};
    PfnItem_GetActualItemClassification ItemGetActualClassification = nullptr;
    PfnGameEngine_GetItemColor GameGetItemColor = nullptr;
    PfnGameEngine_GetItemBackgroundOpacity GameGetItemBackgroundOpacity = nullptr;
    PfnEngine_GetOptions EngineGetOptions = nullptr;
    PfnOptions_GetBool OptionsGetBool = nullptr;
    PfnInvCtrl_GetNumberOfSacks InvCtrlGetNumberOfSacks = nullptr;
    PfnInvCtrl_GetSack InvCtrlGetSack = nullptr;
    PfnPlayer_IsInventorySpaceAvailable PlayerIsInventorySpaceAvailable = nullptr;
    PfnPlayer_PlayInventoryFullSound PlayerPlayInventoryFullSound = nullptr;

    const void* ItemVftable = nullptr;
    PfnItem_CreateItem ItemCreateItem = nullptr;
    PfnItem_GetItemReplicaInfo ItemGetItemReplicaInfo = nullptr;
    PfnItem_GetInt ItemGetItemClassification = nullptr;
    PfnItem_GetInt ItemGetItemType = nullptr;
    PfnItem_GetNumberInStack ItemGetNumberInStack = nullptr;

    PfnCrt_OperatorDelete CrtOperatorDelete = nullptr;

    // ---- DECODED at resolve time (ut_bindings.h rows) - 0 / -1 when the decode failed ----------
    unsigned stashOff = 0;     // GetPlayerStash       lea eax,[ecx+d32]; ret
    unsigned transferOff = 0;  // GetPlayerTransfer
    unsigned relicOff = 0;     // GetPlayerRelicVault
    unsigned modeOff = 0;      // GetCaravanMode       mov eax,[ecx+d32]; ret
    unsigned cursorIdOff = 0;  // CursorHandlerItemMove::GetId  mov eax,[ecx+d8]; ret
    int itemSlotReplica = -1;  // byte offsets in ??_7Item, found BY ADDRESS
    int itemSlotClass = -1;
    int itemSlotType = -1;
    int itemSlotStack = -1;
    // The exe signatures (the view hooks / tests them). Module-relative, 0 = none.
    unsigned sigTransferPageRva = 0;
    unsigned sigStreamOutRva = 0;
    // the two refused pick-up paths, and what the decoder reads out of the matched bytes.
    unsigned sigRightClickRva = 0;
    unsigned sigHeldPickupRva = 0;
    unsigned sigLeftClickRva = 0;         // the page left-click
    const void* retRightClick = nullptr;  // the return address of its GetItemUnderPoint call
    const void* retHeldPickup = nullptr;
    const void* retLeftClick = nullptr;
    // the page mouse handler's entry (exe.leftClickHandler) and the frame decoded from it:
    // at its GetItemUnderPoint call, EBP - return slot == leftFrameDist + (EBP & 7). 0 = unknown
    // (the hover then stays refused: no tooltips, nothing else changes).
    unsigned sigLeftHandlerRva = 0;
    unsigned leftFrameDist = 0;
    // the quick-move's primary AddItemToTransfer(id) call returns here (TQ.exe 0x107A6C,
    // decoded row exe.quickMoveAdd.ret); null = unknown (the quick-move deposit is refused).
    const void* retQuickMove = nullptr;
    unsigned sigPageDrawRva = 0;   // exe.transferPageDraw (the slot plates), 0 = none
    unsigned sigItemBackgroundRva = 0;   // exe.itemBackground (the rect route), 0 = none
    unsigned pageUiOff = 0;      // sub-window -> UIStashInventory (lea edi,[ebx+d32]) = 0x88
    unsigned pageSackOff = 0;    // UIStashInventory -> cached sack (mov [edi+d8],ecx) = 0x60
    // (row cursor.disposal): decoded from PrimaryTransferActivate's own disposal bytes
    // `8B 43 d8 / FF B0 d32 ... E8 SendRemoveItemFromInventory`: the handler's player at
    // [handler + d8] (0x14) and the controller id at [player + d32] (0xC10, which must equal what
    // Character::GetControllerId reads). 0 = not decoded: the drag-drop deposit stays refused.
    unsigned cursorPlayerOff = 0;
    unsigned ctrlIdOff = 0;

    // Count of required symbols that came back MISSING.
    int missingRequired = 0;
    // Count of symbols that DID resolve by name, required or not - the "by export" number in the
    // bindings gate's one-line summary.
    int resolvedByName = 0;
};

namespace ut {

extern TqRuntime g_tq;

// Blocks until GetModuleHandle finds Engine.dll and Game.dll (poll every 50 ms), then returns.
// false = the modules never appeared within timeoutMs. MSVCR110.dll is looked up, never loaded.
bool waitForGameModules(DWORD timeoutMs);

// GetProcAddress for everything in TqRuntime; logs address or MISSING per symbol.
bool resolveExports();

// The DECODED and SIGNATURE rows of ut_bindings.h: the offsets out of the getters' own bytes, the
// Item / CursorHandlerItemMove vftable slots found by address, the two counted TQ.exe signatures.
// Every outcome is reported through bindingsNote; the gate decides. Worker thread, before any hook.
void decodeBindings();

// ---- guarded memory access (VirtualQuery + SEH; never faults, never throws) ------------------
bool readable(const void* p, size_t n);
bool safeRead(const void* p, void* out, size_t n);
// The GameEngine / Engine singletons out of their data exports, or null.
TqGameEngine* gameEngine();
TqEngine* engine();
// The id in a CursorHandlerItemMove, through the decoded offset. 0 when it cannot be read.
unsigned cursorItemId(const void* cursor);
// A VS2012 std::string's text, at most cap-1 chars, non-printables as '?'. False if unreadable.
bool safeStdString(const void* s, char* out, size_t cap);

// SEH-guard depth of the current thread: the fault watchdog ignores the mod's own guarded reads.
int guardDepth();
// bracket an SEH-guarded engine call so the watchdog does not report the mod's own guarded read.
void utGuardEnter();
void utGuardLeave();

// "Engine.dll+0x1234" for an address inside one of the modules the mod knows, else "0x12345678".
// Any thread; never calls into the loader (the module ranges are taken once, by the worker).
void fmtAddr(const void* a, char* out, size_t cap);

// The watchdog's one fault line (GD reagentLogFault's job): what, where, the access.
void logFault(const char* what, const EXCEPTION_RECORD* rec, DWORD tid);

}  // namespace ut

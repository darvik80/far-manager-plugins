#ifndef FAR_PLUGIN_HPP_
#define FAR_PLUGIN_HPP_

#include <windows.h>
#include <guiddef.h>
#include <cstdint>

#define FARMANAGERVERSION_MAJOR 3
#define FARMANAGERVERSION_MINOR 0
#define FARMANAGERVERSION_REVISION 0
#define FARMANAGERVERSION_BUILD 6699
#define FARMANAGERVERSION_STAGE VS_RELEASE

enum VERSION_STAGE {
    VS_RELEASE = 0,
    VS_ALPHA,
    VS_BETA,
    VS_RC,
    VS_SPECIAL,
    VS_PRIVATE
};

struct VersionInfo {
    DWORD Major;
    DWORD Minor;
    DWORD Revision;
    DWORD Build;
    DWORD Stage;
};

#define MAKEFARVERSION(major, minor, rev, build, stage) \
    ((int)(((major) << 28) | ((minor) << 24) | ((rev) << 20) | ((build) & 0xFFFFF)))

#define FARMANAGERVERSION MAKEFARVERSION( \
    FARMANAGERVERSION_MAJOR, FARMANAGERVERSION_MINOR, \
    FARMANAGERVERSION_REVISION, FARMANAGERVERSION_BUILD, \
    FARMANAGERVERSION_STAGE)

typedef intptr_t FARINT;

enum FARCOLORFLAGS : unsigned long long {
    FCF_FG_INDEX           = 0x0000000000000001ULL,
    FCF_BG_INDEX           = 0x0000000000000002ULL,
    FCF_FG_UNDERLINE_INDEX = 0x0000000000000008ULL,
    FCF_INDEXMASK          = 0x000000000000000BULL,
    FCF_INHERIT_STYLE      = 0x0000000000000004ULL,
    FCF_RAWATTR_MASK       = 0x000000000000FF00ULL,
    FCF_FG_BOLD            = 0x1000000000000000ULL,
    FCF_FG_ITALIC          = 0x2000000000000000ULL,
    FCF_FG_U_DATA0         = 0x4000000000000000ULL,
    FCF_FG_U_DATA1         = 0x8000000000000000ULL,
    FCF_FG_OVERLINE        = 0x0100000000000000ULL,
    FCF_FG_STRIKEOUT       = 0x0200000000000000ULL,
    FCF_FG_FAINT           = 0x0400000000000000ULL,
    FCF_FG_BLINK           = 0x0800000000000000ULL,
    FCF_INVERSE            = 0x0010000000000000ULL,
    FCF_FG_INVISIBLE       = 0x0020000000000000ULL,
    FCF_FG_U_DATA2         = 0x0040000000000000ULL,
    FCF_FG_UNDERLINE_MASK  = 0xC040000000000000ULL,
    FCF_STYLE_MASK         = 0xFFF0000000000000ULL,
    FCF_NONE               = 0,
    FCF_FG_4BIT            = 0x0000000000000001ULL,
    FCF_BG_4BIT            = 0x0000000000000002ULL,
    FCF_4BITMASK           = 0x000000000000000BULL,
    FCF_FG_4BIT_MASK       = 0x0000000000000003ULL,
};

struct FarColor {
    uint64_t Flags;
    union {
        COLORREF ForegroundColor;
        struct {
            unsigned char ForegroundR, ForegroundG, ForegroundB;
        };
    };
    union {
        COLORREF BackgroundColor;
        struct {
            unsigned char BackgroundR, BackgroundG, BackgroundB;
        };
    };
    union {
        COLORREF UnderlineColor;
        struct {
            unsigned char UnderlineR, UnderlineG, UnderlineB;
        };
    };
    DWORD Reserved;

    FarColor() : Flags(0), ForegroundColor(0), BackgroundColor(0), UnderlineColor(0), Reserved(0) {}
};

enum FARDIALOGITEMTYPES {
    DI_TEXT = 0,
    DI_VTEXT = 1,
    DI_SINGLEBOX = 2,
    DI_DOUBLEBOX = 3,
    DI_EDIT = 4,
    DI_PSWEDIT = 5,
    DI_FIXEDIT = 6,
    DI_BUTTON = 7,
    DI_CHECKBOX = 8,
    DI_RADIOBUTTON = 9,
    DI_COMBOBOX = 10,
    DI_LISTBOX = 11,
    DI_USERCONTROL = 255,
};

enum FARDIALOGITEMFLAGS : unsigned long long {
    DIF_NONE                  = 0,
    DIF_BOXCOLOR              = 0x0000000000000200ULL,
    DIF_GROUP                 = 0x0000000000000400ULL,
    DIF_LEFTTEXT              = 0x0000000000000800ULL,
    DIF_MOVESELECT            = 0x0000000000001000ULL,
    DIF_SHOWAMPERSAND         = 0x0000000000002000ULL,
    DIF_CENTERGROUP           = 0x0000000000004000ULL,
    DIF_NOBRACKETS            = 0x0000000000008000ULL,
    DIF_MANUALADDHISTORY      = 0x0000000000008000ULL,
    DIF_SEPARATOR             = 0x0000000000010000ULL,
    DIF_SEPARATOR2            = 0x0000000000020000ULL,
    DIF_EDITOR                = 0x0000000000020000ULL,
    DIF_LISTNOAMPERSAND       = 0x0000000000020000ULL,
    DIF_LISTNOBOX             = 0x0000000000040000ULL,
    DIF_HISTORY               = 0x0000000000040000ULL,
    DIF_BTNNOCLOSE            = 0x0000000000040000ULL,
    DIF_CENTERTEXT            = 0x0000000000040000ULL,
    DIF_SEPARATORUSER         = 0x0000000000080000ULL,
    DIF_SETSHIELD             = 0x0000000000080000ULL,
    DIF_EDITEXPAND            = 0x0000000000080000ULL,
    DIF_DROPDOWNLIST          = 0x0000000000100000ULL,
    DIF_USELASTHISTORY        = 0x0000000000200000ULL,
    DIF_MASKEDIT              = 0x0000000000400000ULL,
    DIF_LISTTRACKMOUSE        = 0x0000000000400000ULL,
    DIF_LISTTRACKMOUSEINFOCUS = 0x0000000000800000ULL,
    DIF_SELECTONENTRY         = 0x0000000000800000ULL,
    DIF_3STATE                = 0x0000000000800000ULL,
    DIF_EDITPATH              = 0x0000000001000000ULL,
    DIF_LISTWRAPMODE          = 0x0000000001000000ULL,
    DIF_NOAUTOCOMPLETE        = 0x0000000002000000ULL,
    DIF_LISTAUTOHIGHLIGHT     = 0x0000000002000000ULL,
    DIF_LISTNOCLOSE           = 0x0000000004000000ULL,
    DIF_EDITPATHEXEC          = 0x0000000004000000ULL,
    DIF_HIDDEN                = 0x0000000010000000ULL,
    DIF_READONLY              = 0x0000000020000000ULL,
    DIF_NOFOCUS               = 0x0000000040000000ULL,
    DIF_DISABLE               = 0x0000000080000000ULL,
    DIF_DEFAULTBUTTON         = 0x0000000100000000ULL,
    DIF_FOCUS                 = 0x0000000200000000ULL,
    DIF_RIGHTTEXT             = 0x0000000400000000ULL,
    DIF_WORDWRAP              = 0x0000000800000000ULL,
    DIF_HOMEITEM              = 0x0000002000000000ULL,
};

enum FARMESSAGEFLAGS {
    FMSG_WARNING             = 0x0000000000000001ULL,
    FMSG_ERRORTYPE           = 0x0000000000000002ULL,
    FMSG_KEEPBACKGROUND      = 0x0000000000000004ULL,
    FMSG_LEFTALIGN           = 0x0000000000000008ULL,
    FMSG_ALLINONE            = 0x0000000000000010ULL,
    FMSG_MB_OK               = 0x0000000000010000ULL,
    FMSG_MB_OKCANCEL         = 0x0000000000020000ULL,
    FMSG_MB_ABORTRETRYIGNORE = 0x0000000000030000ULL,
    FMSG_MB_YESNO            = 0x0000000000040000ULL,
    FMSG_MB_YESNOCANCEL      = 0x0000000000050000ULL,
    FMSG_MB_RETRYCANCEL      = 0x0000000000060000ULL,
    FMSG_NONE                = 0,
};

enum DIALOG_MESSAGES {
    DM_FIRST                        = 0,
    DM_CLOSE                        = 1,
    DM_ENABLE                       = 2,
    DM_ENABLEREDRAW                 = 3,
    DM_GETDLGDATA                   = 4,
    DM_GETDLGITEM                   = 5,
    DM_GETDLGRECT                   = 6,
    DM_GETTEXT                      = 7,
    DM_KEY                          = 9,
    DM_MOVEDIALOG                   = 10,
    DM_SETDLGDATA                   = 11,
    DM_SETDLGITEM                   = 12,
    DM_SETFOCUS                     = 13,
    DM_REDRAW                       = 14,
    DM_SETTEXT                      = 15,
    DM_SETMAXTEXTLENGTH             = 16,
    DM_SHOWDIALOG                   = 17,
    DM_GETFOCUS                     = 18,
    DM_GETCURSORPOS                 = 19,
    DM_SETCURSORPOS                 = 20,
    DM_SETTEXTPTR                   = 22,
    DM_SHOWITEM                     = 23,
    DM_ADDHISTORY                   = 24,
    DM_GETCHECK                     = 25,
    DM_SETCHECK                     = 26,
    DM_SET3STATE                    = 27,
    DM_LISTSORT                     = 28,
    DM_LISTGETITEM                  = 29,
    DM_LISTGETCURPOS                = 30,
    DM_LISTSETCURPOS                = 31,
    DM_LISTDELETE                   = 32,
    DM_LISTADD                      = 33,
    DM_LISTADDSTR                   = 34,
    DM_LISTUPDATE                   = 35,
    DM_LISTINSERT                   = 36,
    DM_LISTFINDSTRING               = 37,
    DM_LISTINFO                     = 38,
    DM_LISTGETDATA                  = 39,
    DM_LISTSETDATA                  = 40,
    DM_LISTSETTITLES                = 41,
    DM_LISTGETTITLES                = 42,
    DM_RESIZEDIALOG                 = 43,
    DM_SETITEMPOSITION              = 44,
    DM_GETDROPDOWNOPENED            = 45,
    DM_SETDROPDOWNOPENED            = 46,
    DM_SETHISTORY                   = 47,
    DM_GETITEMPOSITION              = 48,
    DM_SETINPUTNOTIFY               = 49,
    DM_SETMOUSEEVENTNOTIFY          = DM_SETINPUTNOTIFY,
    DM_EDITUNCHANGEDFLAG            = 50,
    DM_GETITEMDATA                  = 51,
    DM_SETITEMDATA                  = 52,
    DM_LISTSET                      = 53,
    DM_GETCURSORSIZE                = 54,
    DM_SETCURSORSIZE                = 55,
    DM_LISTGETDATASIZE              = 56,
    DM_GETSELECTION                 = 57,
    DM_SETSELECTION                 = 58,
    DM_GETEDITPOSITION              = 59,
    DM_SETEDITPOSITION              = 60,
    DM_SETCOMBOBOXEVENT             = 61,
    DM_GETCOMBOBOXEVENT             = 62,
    DM_GETCONSTTEXTPTR              = 63,
    DM_GETDLGITEMSHORT              = 64,
    DM_SETDLGITEMSHORT              = 65,
    DM_GETDIALOGINFO                = 66,
    DM_GETDIALOGTITLE               = 67,

    DN_FIRST                        = 4096,
    DN_BTNCLICK                     = 4097,
    DN_CTLCOLORDIALOG               = 4098,
    DN_CTLCOLORDLGITEM              = 4099,
    DN_CTLCOLORDLGLIST              = 4100,
    DN_DRAWDIALOG                   = 4101,
    DN_DRAWDLGITEM                  = 4102,
    DN_EDITCHANGE                   = 4103,
    DN_GOTFOCUS                     = 4105,
    DN_HELP                         = 4106,
    DN_HOTKEY                       = 4107,
    DN_INITDIALOG                   = 4108,
    DN_KILLFOCUS                    = 4109,
    DN_LISTCHANGE                   = 4110,
    DN_DRAGGED                      = 4111,
    DN_RESIZECONSOLE                = 4112,
    DN_DRAWDIALOGDONE               = 4113,
    DN_LISTHOTKEY                   = 4114,
    DN_INPUT                        = 4115,
    DN_CONTROLINPUT                 = 4116,
    DN_CLOSE                        = 4117,
    DN_GETVALUE                     = 4118,
    DN_DROPDOWNOPENED               = 4119,
    DN_DRAWDLGITEMDONE              = 4120,

    DM_USER                         = 0x4000,
};

enum FARCHECKEDSTATE {
    BSTATE_UNCHECKED = 0,
    BSTATE_CHECKED   = 1,
    BSTATE_3STATE    = 2,
    BSTATE_TOGGLE    = 3,
};

enum LISTITEMFLAGS {
    LIF_SELECTED     = 0x0001,
    LIF_CHECKED      = 0x0002,
    LIF_SEPARATOR    = 0x0004,
    LIF_DISABLE      = 0x0008,
    LIF_GRAYED       = 0x0010,
    LIF_HIDDEN       = 0x0020,
    LIF_DELETEUSERDATA = 0x8000,
};

struct FarListItem {
    uint64_t Flags;
    const wchar_t* Text;
    intptr_t UserData;
    intptr_t Reserved;
};

struct FarList {
    size_t StructSize;
    size_t ItemsNumber;
    FarListItem* Items;
};

struct FarListUpdate {
    size_t StructSize;
    size_t Index;
    FarListItem Item;
};

struct FarListInfo {
    size_t StructSize;
    uint64_t Flags;
    size_t ItemsNumber;
    intptr_t SelectPos;
    intptr_t TopPos;
    intptr_t MaxHeight;
    intptr_t MaxLength;
};

struct FarListPos {
    size_t StructSize;
    intptr_t SelectPos;
    intptr_t TopPos;
    uint64_t Flags;
};

struct FAR_CHAR_INFO {
    wchar_t Char;
    wchar_t Reserved0;
    int Reserved1;
    FarColor Attributes;
};

struct FarDialogItem {
    int Type;
    intptr_t X1, Y1, X2, Y2;
    union {
        intptr_t Selected;
        FarList* ListItems;
        FAR_CHAR_INFO* VBuf;
        intptr_t Reserved0;
    } Param;
    const wchar_t* History;
    const wchar_t* Mask;
    uint64_t Flags;
    const wchar_t* Data;
    intptr_t MaxLength;
    intptr_t UserData;
    intptr_t Reserved[2];
};

struct FarDialogItemData {
    size_t StructSize;
    intptr_t PtrLength;
    wchar_t* PtrData;
};

struct FarDialogItemColors {
    size_t StructSize;
    uint64_t Flags;
    size_t ColorsCount;
    FarColor* Colors;
};

struct PluginMenuItem {
    const GUID* Guids;
    const wchar_t* const* Strings;
    size_t Count;
};

enum OPENFROM {
    OPEN_LEFTDISKMENU  = 0,
    OPEN_PLUGINSMENU   = 1,
    OPEN_FINDLIST      = 2,
    OPEN_SHORTCUT      = 3,
    OPEN_COMMANDLINE   = 4,
    OPEN_EDITOR        = 5,
    OPEN_VIEWER        = 6,
    OPEN_FILEPANEL     = 7,
    OPEN_DIALOG        = 8,
    OPEN_ANALYSE       = 9,
    OPEN_RIGHTDISKMENU = 10,
    OPEN_FROMMACRO     = 11,
    OPEN_LUAMACRO      = 100,
};

struct GlobalInfo {
    size_t StructSize;
    VersionInfo MinFarVersion;
    VersionInfo Version;
    GUID Guid;
    const wchar_t* Title;
    const wchar_t* Description;
    const wchar_t* Author;
    void* Instance;
};

struct PluginInfo {
    size_t StructSize;
    uint64_t Flags;
    PluginMenuItem DiskMenu;
    PluginMenuItem PluginMenu;
    PluginMenuItem PluginConfig;
    const wchar_t* CommandPrefix;
    void* Instance;
};

struct OpenInfo {
    size_t StructSize;
    int OpenFrom;
    const GUID* Guid;
    intptr_t Data;
    void* Instance;
};

struct OpenCommandLineInfo {
    size_t StructSize;
    const wchar_t* CommandLine;
};

struct ExitInfo {
    size_t StructSize;
    void* Instance;
};

struct ConfigureInfo {
    size_t StructSize;
    GUID Guid;
    void* Instance;
};

struct ProcessPanelInputInfo {
    size_t StructSize;
    HANDLE hPanel;
    INPUT_RECORD Rec;
    void* Instance;
};

struct ProcessConsoleInputInfo {
    size_t StructSize;
    uint64_t Flags;
    INPUT_RECORD Rec;
    void* Instance;
};

struct FarStandardFunctions {
    size_t StructSize;
    // Standard functions - simplified for now
    int (WINAPI *atoi)(const char*);
    uint64_t (WINAPI *atoi64)(const char*);
    char* (WINAPI *itoa)(int, char*, int);
    char* (WINAPI *itoa64)(uint64_t, char*, int);
    int (WINAPI *sprintf)(char*, const char*, ...);
    int (WINAPI *sscanf)(const char*, const char*, ...);
    int (WINAPI *snprintf)(char*, size_t, const char*, ...);
};

struct FarKey {
    WORD VirtualKeyCode;
    DWORD ControlKeyState;
};

struct FarMenuItem {
    uint64_t Flags;
    const wchar_t* Text;
    FarKey AccelKey;
    intptr_t UserData;
    intptr_t Reserved[2];
};

typedef intptr_t (WINAPI *FARAPIMENU)(
    const GUID& PluginId,
    const GUID& Id,
    int X, int Y,
    int MaxHeight,
    uint64_t Flags,
    const wchar_t* Title,
    const wchar_t* Bottom,
    const wchar_t* HelpTopic,
    const int* BreakKeys,
    int* BreakCode,
    const FarMenuItem* Items,
    size_t ItemsNumber);

typedef int (WINAPI *FARAPIMESSAGE)(
    const GUID& PluginId,
    const GUID& Id,
    uint64_t Flags,
    const wchar_t* HelpTopic,
    const wchar_t* const* Items,
    size_t ItemsNumber,
    intptr_t ButtonsNumber);

typedef const wchar_t* (WINAPI *FARAPIGETMSG)(
    const GUID& PluginId,
    int Msg);

typedef intptr_t (WINAPI *FARAPIPANELCONTROL)(
    HANDLE hPanel,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef HANDLE (WINAPI *FARAPISAVESCREEN)(const GUID& PluginId);
typedef void (WINAPI *FARAPIRESTORESCREEN)(const GUID& PluginId, HANDLE Handle);
typedef int (WINAPI *FARAPIGETDIRLIST)(const wchar_t*, void**);
typedef int (WINAPI *FARAPIGETPLUGINDIRLIST)(const GUID&, const wchar_t*, void**);
typedef void (WINAPI *FARAPIFREEDIRLIST)(void*);
typedef void (WINAPI *FARAPIFREEPLUGINDIRLIST)(const GUID&, void*);

typedef HANDLE (WINAPI *FARAPIVIEWER)(
    const wchar_t* Name,
    const wchar_t* Title,
    int X1, int Y1, int X2, int Y2,
    uint64_t Flags,
    unsigned int CodePage);

typedef HANDLE (WINAPI *FARAPIEDITOR)(
    const wchar_t* Name,
    const wchar_t* Title,
    int X1, int Y1, int X2, int Y2,
    uint64_t Flags,
    int StartLine,
    int StartChar,
    unsigned int CodePage);

typedef void (WINAPI *FARAPITEXT)(
    int X, int Y,
    const FarColor* Color,
    const wchar_t* Str);

typedef intptr_t (WINAPI *FARAPIEDITORCONTROL)(
    int Command,
    intptr_t Param1,
    void* Param2);

typedef void (WINAPI *FARAPISHOWHELP)(
    const wchar_t* Name,
    const wchar_t* Topic,
    uint64_t Flags);

typedef intptr_t (WINAPI *FARAPIADVCONTROL)(
    const GUID& PluginId,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIINPUTBOX)(
    const GUID& PluginId,
    const GUID& Id,
    const wchar_t* Title,
    const wchar_t* SubTitle,
    const wchar_t* HistoryName,
    const wchar_t* SrcText,
    wchar_t* DestText,
    size_t DestSize,
    const wchar_t* HelpTopic,
    uint64_t Flags);

typedef int (WINAPI *FARAPICOLORDIALOG)(
    const GUID& PluginId,
    const GUID& Id,
    FarColor* Color);

typedef HANDLE (WINAPI *FARAPIDIALOGINIT)(
    const GUID& PluginId,
    const GUID& Id,
    int X1, int Y1,
    int X2, int Y2,
    const wchar_t* HelpTopic,
    const FarDialogItem* Item,
    size_t ItemsNumber,
    intptr_t Reserved,
    uint64_t Flags,
    intptr_t (WINAPI *DlgProc)(HANDLE, intptr_t, intptr_t, void*),
    intptr_t Param);

typedef intptr_t (WINAPI *FARAPIDIALOGRUN)(HANDLE hDlg);
typedef void (WINAPI *FARAPIDIALOGFREE)(HANDLE hDlg);

typedef intptr_t (WINAPI *FARAPISENDDLGMESSAGE)(
    HANDLE hDlg,
    intptr_t Msg,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIDEFDLGPROC)(
    HANDLE hDlg,
    intptr_t Msg,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIVIEWERCONTROL)(
    int Command,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIPLUGINSCONTROL)(
    HANDLE Handle,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIFILEFILTERCONTROL)(
    HANDLE Handle,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIREGEXPCONTROL)(
    HANDLE Handle,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPIMACROCONTROL)(
    const GUID& PluginId,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef intptr_t (WINAPI *FARAPISETTINGSCONTROL)(
    HANDLE Handle,
    int Command,
    intptr_t Param1,
    void* Param2);

typedef void (WINAPI *FARAPIFREESCREEN)(HANDLE Handle);

struct PluginStartupInfo {
    size_t StructSize;
    const wchar_t* ModuleName;
    FARAPIMENU Menu;
    FARAPIMESSAGE Message;
    FARAPIGETMSG GetMsg;
    FARAPIPANELCONTROL PanelControl;
    FARAPISAVESCREEN SaveScreen;
    FARAPIRESTORESCREEN RestoreScreen;
    FARAPIGETDIRLIST GetDirList;
    FARAPIGETPLUGINDIRLIST GetPluginDirList;
    FARAPIFREEDIRLIST FreeDirList;
    FARAPIFREEPLUGINDIRLIST FreePluginDirList;
    FARAPIVIEWER Viewer;
    FARAPIEDITOR Editor;
    FARAPITEXT Text;
    FARAPIEDITORCONTROL EditorControl;
    FarStandardFunctions* FSF;
    FARAPISHOWHELP ShowHelp;
    FARAPIADVCONTROL AdvControl;
    FARAPIINPUTBOX InputBox;
    FARAPICOLORDIALOG ColorDialog;
    FARAPIDIALOGINIT DialogInit;
    FARAPIDIALOGRUN DialogRun;
    FARAPIDIALOGFREE DialogFree;
    FARAPISENDDLGMESSAGE SendDlgMessage;
    FARAPIDEFDLGPROC DefDlgProc;
    FARAPIVIEWERCONTROL ViewerControl;
    FARAPIPLUGINSCONTROL PluginsControl;
    FARAPIFILEFILTERCONTROL FileFilterControl;
    FARAPIREGEXPCONTROL RegExpControl;
    FARAPIMACROCONTROL MacroControl;
    FARAPISETTINGSCONTROL SettingsControl;
    const void* Private;
    void* Instance;
    FARAPIFREESCREEN FreeScreen;
};

enum PANELINFOFLAGS {
    PFLAGS_SHOWHIDDEN    = 0x00000001,
    PFLAGS_HIGHLIGHT     = 0x00000002,
    PFLAGS_REVERSESORTORDER = 0x00000004,
    PFLAGS_USESORTGROUPS = 0x00000008,
    PFLAGS_SELECTEDFIRST = 0x00000010,
    PFLAGS_REALNAMES     = 0x00000020,
    PFLAGS_PANELLEFT     = 0x00000080,
};

enum PANELTYPE {
    PTYPE_FILEPANEL = 0,
    PTYPE_TREEPANEL = 1,
    PTYPE_QVIEWPANEL = 2,
    PTYPE_INFOPANEL = 3,
};

struct PanelInfo {
    size_t StructSize;
    HANDLE PluginHandle;
    GUID OwnerGuid;
    uint64_t Flags;
    size_t ItemsNumber;
    size_t SelectedItemsNumber;
    RECT PanelRect;
    size_t CurrentItem;
    size_t TopPanelItem;
    intptr_t ViewMode;
    int PanelType;
    int SortMode;
};

struct FarPanelDirectory {
    size_t StructSize;
    const wchar_t* Name;
    const wchar_t* Param;
    GUID PluginId;
    const wchar_t* File;
};

typedef void (*FARPANELITEMFREECALLBACK)(void* UserData, struct PluginPanelItem* PanelItem);

struct UserDataItem {
    void* Data;
    FARPANELITEMFREECALLBACK FreeData;
};

enum PANELITEMFLAGS {
    PPIF_SELECTED     = 0x00000001,
    PPIF_PROCESSDESCR = 0x00000002,
    PPIF_COLUMN       = 0x00000004,
    PPIF_USERDATA     = 0x80000000,
};

struct PluginPanelItem {
    FILETIME CreationTime;
    FILETIME LastAccessTime;
    FILETIME LastWriteTime;
    FILETIME ChangeTime;
    unsigned long long FileSize;
    unsigned long long AllocationSize;
    const wchar_t* FileName;
    const wchar_t* AlternateFileName;
    const wchar_t* Description;
    const wchar_t* Owner;
    const wchar_t* const* CustomColumnData;
    size_t CustomColumnNumber;
    uint64_t Flags;
    struct UserDataItem UserData;
    uintptr_t FileAttributes;
    uintptr_t NumberOfLinks;
    uintptr_t CRC32;
    intptr_t Reserved[2];
};

struct FarGetPanelItem {
    size_t StructSize;
    size_t Size;
    PluginPanelItem* Item;
};

enum FILE_CONTROL_COMMANDS {
    FCTL_CLOSEPANEL                 = 0,
    FCTL_GETPANELINFO               = 1,
    FCTL_UPDATEPANEL                = 2,
    FCTL_REDRAWPANEL                = 3,
    FCTL_GETCMDLINE                 = 4,
    FCTL_SETCMDLINE                 = 5,
    FCTL_SETSELECTION               = 6,
    FCTL_SETVIEWMODE                = 7,
    FCTL_INSERTCMDLINE              = 8,
    FCTL_SETUSERSCREEN              = 9,
    FCTL_SETPANELDIRECTORY          = 10,
    FCTL_SETCMDLINEPOS              = 11,
    FCTL_GETCMDLINEPOS              = 12,
    FCTL_SETSORTMODE                = 13,
    FCTL_SETSORTORDER               = 14,
    FCTL_SETCMDLINESELECTION        = 15,
    FCTL_GETCMDLINESELECTION        = 16,
    FCTL_CHECKPANELSEXIST           = 17,
    FCTL_GETUSERSCREEN              = 19,
    FCTL_ISACTIVEPANEL              = 20,
    FCTL_GETPANELITEM               = 21,
    FCTL_GETSELECTEDPANELITEM       = 22,
    FCTL_GETCURRENTPANELITEM        = 23,
    FCTL_GETPANELDIRECTORY          = 24,
    FCTL_GETCOLUMNTYPES             = 25,
    FCTL_GETCOLUMNWIDTHS            = 26,
    FCTL_BEGINSELECTION             = 27,
    FCTL_ENDSELECTION               = 28,
    FCTL_CLEARSELECTION             = 29,
    FCTL_SETDIRECTORIESFIRST        = 30,
    FCTL_GETPANELFORMAT             = 31,
    FCTL_GETPANELHOSTFILE           = 32,
    FCTL_GETPANELPREFIX             = 34,
    FCTL_SETACTIVEPANEL             = 35,
};

#define PANEL_ACTIVE  ((HANDLE)(-1))
#define PANEL_PASSIVE ((HANDLE)(-2))

enum FARMENUITEMFLAGS {
    MIF_SELECTED   = 0x0001,
    MIF_CHECKED    = 0x0002,
    MIF_SEPARATOR  = 0x0004,
    MIF_DISABLE    = 0x0008,
    MIF_GRAYED     = 0x0010,
    MIF_HIDDEN     = 0x0020,
    MIF_AUTOASSIGNED = 0x0800,
    MIF_SUBMENU    = 0x1000,
    MIF_USEHOTKEY  = 0x2000,
    MIF_USETEXT    = 0x4000,
    MIF_KEEPSELECT = 0x8000,
};

#define FDLG_WARNING             0x0000000000000001ULL
#define FDLG_SMALLDIALOG         0x0000000000000002ULL
#define FDLG_NODRAWSHADOW        0x0000000000000004ULL
#define FDLG_NODRAWPANEL         0x0000000000000008ULL
#define FDLG_KEEPCONSOLETITLE    0x0000000000000010ULL
#define FDLG_NONMODAL            0x0000000000000020ULL
#define FDLG_STAY_ON_TOP         0x0000000000000040ULL
#define FDLG_NONE                0

#ifdef __cplusplus
extern "C" {
#endif

void     WINAPI SetStartupInfoW(const struct PluginStartupInfo* Info);
void     WINAPI GetGlobalInfoW(struct GlobalInfo* Info);
void     WINAPI GetPluginInfoW(struct PluginInfo* Info);
HANDLE   WINAPI OpenW(const struct OpenInfo* Info);
intptr_t WINAPI ConfigureW(const struct ConfigureInfo* Info);
void     WINAPI ExitFARW(const struct ExitInfo* Info);
intptr_t WINAPI ProcessPanelInputW(const struct ProcessPanelInputInfo* Info);
intptr_t WINAPI ProcessConsoleInputW(struct ProcessConsoleInputInfo* Info);

#ifdef __cplusplus
}
#endif

#endif

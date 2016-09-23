//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  kernelWindow.h
//

// This goes with the file kernelWindow.c

#if !defined(_KERNELWINDOW_H)

#include "kernelCharset.h"
#include "kernelGraphic.h"
#include "kernelMouse.h"
#include "kernelText.h"
#include "kernelVariableList.h"
#include <string.h>
#include <sys/font.h>
#include <sys/paths.h>
#include <sys/window.h>

// Definitions

#define WINDOW_DEFAULT_TITLEBAR_HEIGHT			24
#define WINDOW_DEFAULT_TITLEBAR_MINWIDTH \
	(WINDOW_DEFAULT_TITLEBAR_HEIGHT * 4)
#define WINDOW_DEFAULT_BORDER_THICKNESS			2
#define WINDOW_DEFAULT_SHADING_INCREMENT		15
#define WINDOW_DEFAULT_RADIOBUTTON_SIZE			12
#define WINDOW_DEFAULT_CHECKBOX_SIZE			12
#define WINDOW_DEFAULT_SLIDER_WIDTH				20
#define WINDOW_DEFAULT_MIN_WIDTH \
	(WINDOW_DEFAULT_TITLEBAR_MINWIDTH + (WINDOW_DEFAULT_BORDER_THICKNESS * 2))
#define WINDOW_DEFAULT_MIN_HEIGHT \
	(WINDOW_DEFAULT_TITLEBAR_HEIGHT + (WINDOW_DEFAULT_BORDER_THICKNESS * 2))
#define WINDOW_DEFAULT_MINREST_TRACERS			20
#define WINDOW_DEFAULT_FIXFONT_SMALL_FAMILY		FONT_FAMILY_LIBMONO
#define WINDOW_DEFAULT_FIXFONT_SMALL_FLAGS		FONT_STYLEFLAG_FIXED
#define WINDOW_DEFAULT_FIXFONT_SMALL_POINTS		8
#define WINDOW_DEFAULT_FIXFONT_MEDIUM_FAMILY	FONT_FAMILY_LIBMONO
#define WINDOW_DEFAULT_FIXFONT_MEDIUM_FLAGS		FONT_STYLEFLAG_FIXED
#define WINDOW_DEFAULT_FIXFONT_MEDIUM_POINTS	10
#define WINDOW_DEFAULT_VARFONT_SMALL_FAMILY		FONT_FAMILY_ARIAL
#define WINDOW_DEFAULT_VARFONT_SMALL_FLAGS		FONT_STYLEFLAG_BOLD
#define WINDOW_DEFAULT_VARFONT_SMALL_POINTS		10
#define WINDOW_DEFAULT_VARFONT_MEDIUM_FAMILY	FONT_FAMILY_ARIAL
#define WINDOW_DEFAULT_VARFONT_MEDIUM_FLAGS		FONT_STYLEFLAG_BOLD
#define WINDOW_DEFAULT_VARFONT_MEDIUM_POINTS	12
#define WINDOW_MAX_CHILDREN						32

#define WINFLAG_ICONIFIED						0x0800
#define WINFLAG_VISIBLE							0x0400
#define WINFLAG_ENABLED							0x0200
#define WINFLAG_MOVABLE							0x0100
#define WINFLAG_RESIZABLE						0x00C0
#define WINFLAG_RESIZABLEX						0x0080
#define WINFLAG_RESIZABLEY						0x0040
#define WINFLAG_HASBORDER						0x0020
#define WINFLAG_CANFOCUS						0x0010
#define WINFLAG_HASFOCUS						0x0008
#define WINFLAG_ROOTWINDOW						0x0004
#define WINFLAG_BACKGROUNDTILED					0x0002
#define WINFLAG_DEBUGLAYOUT						0x0001

#define WINNAME_TEMPCONSOLE						"temp console window"
#define WINNAME_ROOTWINDOW						"root window"

typedef struct {
	struct {
		color foreground;
		color background;
		color desktop;
	} color;

	struct {
		int minWidth;
		int minHeight;
		int minRestTracers;
	} window;

	struct {
		int height;
		int minWidth;
	} titleBar;

	struct {
		int thickness;
		int shadingIncrement;
	} border;

	struct {
		int size;
	} radioButton;

	struct {
		int size;
	} checkbox;

	struct {
		int width;
	} slider;

	struct {
		kernelFont *defaultFont;
		struct {
			struct {
				char family[FONT_FAMILY_LEN];
				unsigned flags;
				int points;
				kernelFont *font;
			} small;
			struct {
				char family[FONT_FAMILY_LEN];
				unsigned flags;
				int points;
				kernelFont *font;
			} medium;
		} fixWidth;
		struct {
			struct {
				char family[FONT_FAMILY_LEN];
				unsigned flags;
				int points;
				kernelFont *font;
			} small;
			struct {
				char family[FONT_FAMILY_LEN];
				unsigned flags;
				int points;
				kernelFont *font;
			} medium;
		} varWidth;
	} font;

} kernelWindowVariables;

typedef enum {
	genericComponentType,
	borderComponentType,
	buttonComponentType,
	canvasComponentType,
	checkboxComponentType,
	containerComponentType,
	iconComponentType,
	imageComponentType,
	listComponentType,
	listItemComponentType,
	menuBarComponentType,
	progressBarComponentType,
	radioButtonComponentType,
	scrollBarComponentType,
	sliderComponentType,
	sysContainerComponentType,
	textAreaComponentType,
	textLabelComponentType,
	titleBarComponentType,
	treeComponentType,
	windowType

} kernelWindowObjectType;

// Forward declarations, where necessary
struct _kernelWindow;

// The object that defines a GUI component inside a window
typedef volatile struct _kernelWindowComponent {
	kernelWindowObjectType type;	// Must be first
	kernelWindowObjectType subType;
	volatile struct _kernelWindow *window;
	volatile struct _kernelWindowComponent *container;
	volatile struct _kernelWindow *contextMenu;
	char charSet[CHARSET_NAME_LEN];
	graphicBuffer *buffer;
	int xCoord;
	int yCoord;
	int level;
	int width;
	int height;
	int minWidth;
	int minHeight;
	unsigned flags;
	componentParameters params;
	windowEventStream events;
	void (*eventHandler)(volatile struct _kernelWindowComponent *,
		windowEvent *);
	int doneLayout;
	kernelMousePointer *pointer;
	void *data;

	// Routines for managing this component.  These are set by the
	// kernelWindowComponentNew routine, for things that are common to all
	// components.
	int (*drawBorder)(volatile struct _kernelWindowComponent *, int);
	int (*erase)(volatile struct _kernelWindowComponent *);
	int (*grey)(volatile struct _kernelWindowComponent *);

	// Routines that should be implemented by components that 'contain'
	// or instantiate other components
	int (*add)(volatile struct _kernelWindowComponent *, objectKey);
	int (*delete)(volatile struct _kernelWindowComponent *,
			volatile struct _kernelWindowComponent *);
	int (*numComps)(volatile struct _kernelWindowComponent *);
	int (*flatten)(volatile struct _kernelWindowComponent *,
		volatile struct _kernelWindowComponent **, int *, unsigned);
	int (*layout)(volatile struct _kernelWindowComponent *);
	volatile struct _kernelWindowComponent *
		(*activeComp)(volatile struct _kernelWindowComponent *);
	volatile struct _kernelWindowComponent *
		(*eventComp)(volatile struct _kernelWindowComponent *, windowEvent *);
	int (*setBuffer)(volatile struct _kernelWindowComponent *,
		graphicBuffer *);

	// More routines for managing this component.  These are set by the
	// code which builds the instance of the particular component type
	int (*draw)(volatile struct _kernelWindowComponent *);
	int (*update)(volatile struct _kernelWindowComponent *);
	int (*move)(volatile struct _kernelWindowComponent *, int, int);
	int (*resize)(volatile struct _kernelWindowComponent *, int, int);
	int (*focus)(volatile struct _kernelWindowComponent *, int);
	int (*getData)(volatile struct _kernelWindowComponent *, void *, int);
	int (*setData)(volatile struct _kernelWindowComponent *, void *, int);
	int (*getSelected)(volatile struct _kernelWindowComponent *, int *);
	int (*setSelected)(volatile struct _kernelWindowComponent *, int);
	int (*mouseEvent)(volatile struct _kernelWindowComponent *, windowEvent *);
	int (*keyEvent)(volatile struct _kernelWindowComponent *, windowEvent *);
	int (*destroy)(volatile struct _kernelWindowComponent *);

} kernelWindowComponent;

typedef volatile struct {
	borderType type;

} kernelWindowBorder;

typedef volatile struct {
	char label[WINDOW_MAX_LABEL_LENGTH];
	image buttonImage;
	int state;

} kernelWindowButton;

typedef volatile struct {
	char *text;
	int selected;

} kernelWindowCheckbox;

typedef volatile struct {
	const char name[WINDOW_MAX_LABEL_LENGTH];
	kernelWindowComponent **components;
	int maxComponents;
	int numComponents;
	int numColumns;
	int numRows;

	// Functions
	void (*drawGrid)(kernelWindowComponent *);

} kernelWindowContainer;

// An icon image component
typedef volatile struct {
	int selected;
	image iconImage;
	image selectedImage;
	char labelData[WINDOW_MAX_LABEL_LENGTH];
	char *labelLine[WINDOW_MAX_LABEL_LINES];
	int labelLines;
	int labelWidth;
	char command[MAX_PATH_NAME_LENGTH];

} kernelWindowIcon;

// An image as a window component
typedef volatile struct {
	image image;
	drawMode mode;

} kernelWindowImage;

typedef volatile struct {
	graphicBuffer buffer;

} kernelWindowCanvas;

typedef volatile struct {
	windowListType type;
	int columns;
	int rows;
	int itemWidth;
	int itemHeight;
	int selectMultiple;
	int multiColumn;
	int selectedItem;
	int firstVisibleRow;
	int itemRows;
	kernelWindowComponent *container;
	kernelWindowComponent *scrollBar;

} kernelWindowList;

typedef volatile struct {
	windowListType type;
	objectKey parent;
	listItemParameters params;
	kernelWindowComponent *icon;
	int selected;

} kernelWindowListItem;

typedef volatile struct {
	volatile struct _kernelWindow *raisedMenu;
	volatile struct _kernelWindow *menu[WINDOW_MAX_CHILDREN];
	int menuXCoord[WINDOW_MAX_CHILDREN];
	int menuTitleWidth[WINDOW_MAX_CHILDREN];
	int numMenus;
	kernelWindowComponent *container;

} kernelWindowMenuBar;

typedef kernelWindowListItem kernelWindowMenuItem;

typedef kernelTextArea kernelWindowPasswordField;

typedef volatile struct {
	int progressPercent;
	int sliderWidth;

} kernelWindowProgressBar;

typedef volatile struct {
	char *text;
	int numItems;
	int selectedItem;

} kernelWindowRadioButton;

typedef volatile struct {
	scrollBarType type;
	scrollBarState state;
	int sliderX;
	int sliderY;
	int sliderWidth;
	int sliderHeight;
	int dragging;
	int dragX;
	int dragY;

} kernelWindowScrollBar;

typedef kernelWindowScrollBar kernelWindowSlider;

typedef volatile struct {
	kernelTextArea *area;
	int areaWidth;
	kernelWindowComponent *scrollBar;

	// For derived kernelWindowTextField use
	char *fieldBuffer;

} kernelWindowTextArea;

// A text area as a text field component
typedef kernelTextArea kernelWindowTextField;

typedef volatile struct {
	char *text;
	int lines;

} kernelWindowTextLabel;

typedef volatile struct {
	kernelWindowComponent *minimizeButton;
	kernelWindowComponent *closeButton;

} kernelWindowTitleBar;

typedef volatile struct {
	int numItems;
	windowTreeItem *items;
	int rows;
	int expandedItems;
	int visibleItems;
	int scrolledLines;
	int selectedItem;
	kernelWindowComponent *container;
	kernelWindowComponent *scrollBar;

} kernelWindowTree;

// The object that defines a GUI window
typedef volatile struct _kernelWindow {
	kernelWindowObjectType type;	// Must be first
	int processId;
	char charSet[CHARSET_NAME_LEN];
	char title[WINDOW_MAX_TITLE_LENGTH];
	int xCoord;
	int yCoord;
	int level;
	unsigned flags;
	graphicBuffer buffer;
	image backgroundImage;
	color background;
	windowEventStream events;
	kernelWindowComponent *titleBar;
	kernelWindowComponent *borders[4];
	kernelWindowComponent *menuBar;
	volatile struct _kernelWindow *contextMenu;
	kernelWindowComponent *sysContainer;
	kernelWindowComponent *mainContainer;
	kernelWindowComponent *focusComponent;
	kernelWindowComponent *mouseInComponent;
	kernelMousePointer *pointer;

	// Parent-child relationships
	volatile struct _kernelWindow *parentWindow;
	volatile struct _kernelWindow *child[WINDOW_MAX_CHILDREN];
	int numChildren;
	volatile struct _kernelWindow *dialogWindow;

	// Routines for managing this window
	int (*draw)(volatile struct _kernelWindow *);
	int (*drawClip)(volatile struct _kernelWindow *, int, int, int, int);
	int (*update)(volatile struct _kernelWindow *, int, int, int, int);
	int (*changeComponentFocus)(volatile struct _kernelWindow *,
		kernelWindowComponent *);
	void (*focus)(volatile struct _kernelWindow *, int);
	int (*mouseEvent)(volatile struct _kernelWindow *, kernelWindowComponent *,
		windowEvent *);
	int (*keyEvent)(volatile struct _kernelWindow *, kernelWindowComponent *,
		windowEvent *);

} kernelWindow;

// This is only used internally, to define a coordinate area
typedef struct {
	int leftX;
	int topY;
	int rightX;
	int bottomY;

} screenArea;

#define makeWindowScreenArea(w) \
	(&((screenArea){ (w)->xCoord, (w)->yCoord,		\
		((w)->xCoord + ((w)->buffer.width - 1)),	\
		((w)->yCoord + ((w)->buffer.height - 1)) } ))

#define makeComponentScreenArea(c) \
	(&((screenArea){ ((c)->window->xCoord + (c)->xCoord),		\
		((c)->window->yCoord + (c)->yCoord),					\
		((c)->window->xCoord + (c)->xCoord + ((c)->width - 1)),	\
		((c)->window->yCoord + (c)->yCoord + ((c)->height - 1)) } ))

static inline kernelWindow *getWindow(objectKey object)
{
	if (((kernelWindow *) object)->type == windowType)
		return ((kernelWindow *) object);
	else
		return (((kernelWindowComponent *) object)->window);
}

static inline int isPointInside(int xCoord, int yCoord,	screenArea *area)
{
	// Return 1 if point 1 is inside area 2

	if ((xCoord < area->leftX) || (xCoord > area->rightX) ||
		(yCoord < area->topY) || (yCoord > area->bottomY))
		return (0);
	else
		// Yup, it's inside
		return (1);
}

static inline int doLinesIntersect(int horizX1, int horizY, int horizX2,
	int vertX, int vertY1, int vertY2)
{
	// True if the horizontal line intersects the vertical line

	if ((vertX < horizX1) || (vertX > horizX2) ||
		((horizY < vertY1) || (horizY > vertY2)))
		return (0);
	else
		// Yup, they intersect
		return (1);
}

static inline int doAreasIntersect(screenArea *firstArea,
	screenArea *secondArea)
{
	// Return 1 if area 1 and area 2 intersect.

	if (isPointInside(firstArea->leftX, firstArea->topY, secondArea) ||
		isPointInside(firstArea->rightX, firstArea->topY, secondArea) ||
		isPointInside(firstArea->leftX, firstArea->bottomY, secondArea) ||
		isPointInside(firstArea->rightX, firstArea->bottomY, secondArea) ||
		isPointInside(secondArea->leftX, secondArea->topY, firstArea) ||
		isPointInside(secondArea->rightX, secondArea->topY, firstArea) ||
		isPointInside(secondArea->leftX, secondArea->bottomY, firstArea) ||
		isPointInside(secondArea->rightX, secondArea->bottomY, firstArea))
	{
		return (1);
	}
	else if (doLinesIntersect(firstArea->leftX, firstArea->topY,
			firstArea->rightX, secondArea->leftX, secondArea->topY,
			secondArea->bottomY) ||
		doLinesIntersect(secondArea->leftX, secondArea->topY,
			secondArea->rightX, firstArea->leftX, firstArea->topY,
			firstArea->bottomY))
	{
		return (1);
	}
	else
	{
		// Nope, not intersecting
		return (0);
	}
}

static inline void removeFromContainer(kernelWindowComponent *component)
{
	// Remove the component from its parent container
	if (component->container && component->container->delete)
		component->container->delete(component->container, component);
	component->container = NULL;
}

#ifdef DEBUG
static inline const char *componentTypeString(kernelWindowObjectType type)
{
	// Return a string representation of a window object type.
	switch (type)
	{
		case genericComponentType:
			return ("genericComponentType");
		case borderComponentType:
			return ("borderComponentType");
		case buttonComponentType:
			return ("buttonComponentType");
		case canvasComponentType:
			return ("canvasComponentType");
		case checkboxComponentType:
			return ("checkboxComponentType");
		case containerComponentType:
			return ("containerComponentType");
		case iconComponentType:
			return ("iconComponentType");
		case imageComponentType:
			return ("imageComponentType");
		case listComponentType:
			return ("listComponentType");
		case listItemComponentType:
			return ("listItemComponentType");
		case menuBarComponentType:
			return ("menuBarComponentType");
		case progressBarComponentType:
			return ("progressBarComponentType");
		case radioButtonComponentType:
			return ("radioButtonComponentType");
		case scrollBarComponentType:
			return ("scrollBarComponentType");
		case sliderComponentType:
			return ("sliderComponentType");
		case sysContainerComponentType:
			return ("sysContainerComponentType");
		case textAreaComponentType:
			return ("textAreaComponentType");
		case textLabelComponentType:
			return ("textLabelComponentType");
		case titleBarComponentType:
			return ("titleBarComponentType");
		case windowType:
			return ("windowType");
		default:
			return ("unknown");
	}
}
#endif

// Functions exported by kernelWindow.c
int kernelWindowInitialize(void);
int kernelWindowStart(void);
int kernelWindowLogin(const char *);
int kernelWindowLogout(void);
kernelWindow *kernelWindowNew(int, const char *);
kernelWindow *kernelWindowNewChild(kernelWindow *, const char *);
kernelWindow *kernelWindowNewDialog(kernelWindow *, const char *);
int kernelWindowDestroy(kernelWindow *);
int kernelWindowUpdateBuffer(graphicBuffer *, int, int, int, int);
int kernelWindowSetCharSet(kernelWindow *, const char *);
int kernelWindowSetTitle(kernelWindow *, const char *);
int kernelWindowGetSize(kernelWindow *, int *, int *);
int kernelWindowSetSize(kernelWindow *, int, int);
int kernelWindowGetLocation(kernelWindow *, int *, int *);
int kernelWindowSetLocation(kernelWindow *, int, int);
int kernelWindowCenter(kernelWindow *);
int kernelWindowSnapIcons(objectKey);
int kernelWindowSetHasBorder(kernelWindow *, int);
int kernelWindowSetHasTitleBar(kernelWindow *, int);
int kernelWindowSetMovable(kernelWindow *, int);
int kernelWindowSetResizable(kernelWindow *, int);
int kernelWindowSetFocusable(kernelWindow *, int);
int kernelWindowRemoveMinimizeButton(kernelWindow *);
int kernelWindowRemoveCloseButton(kernelWindow *);
int kernelWindowFocus(kernelWindow *);
int kernelWindowSetVisible(kernelWindow *, int);
void kernelWindowSetMinimized(kernelWindow *, int);
int kernelWindowAddConsoleTextArea(kernelWindow *);
void kernelWindowRedrawArea(int, int, int, int);
int kernelWindowDraw(kernelWindow *);
void kernelWindowDrawAll(void);
int kernelWindowGetColor(const char *, color *);
int kernelWindowSetColor(const char *, color *);
void kernelWindowResetColors(void);
void kernelWindowProcessEvent(windowEvent *);
int kernelWindowRegisterEventHandler(kernelWindowComponent *,
	void (*)(kernelWindowComponent *, windowEvent *));
int kernelWindowComponentEventGet(objectKey, windowEvent *);
int kernelWindowSetBackgroundColor(kernelWindow *, color *);
int kernelWindowSetBackgroundImage(kernelWindow *, image *);
int kernelWindowScreenShot(image *);
int kernelWindowSaveScreenShot(const char *);
int kernelWindowSetTextOutput(kernelWindowComponent *);
int kernelWindowLayout(kernelWindow *);
void kernelWindowDebugLayout(kernelWindow *);
int kernelWindowContextAdd(objectKey, windowMenuContents *);
int kernelWindowContextSet(objectKey, kernelWindow *);
int kernelWindowSwitchPointer(objectKey, const char *);
void kernelWindowMoveConsoleTextArea(kernelWindow *, kernelWindow *);
int kernelWindowToggleMenuBar(void);
int kernelWindowRefresh(void);

// Window shell functions
int kernelWindowShell(const char *);
void kernelWindowShellUpdateList(kernelWindow *[], int);
void kernelWindowShellRefresh(void);
int kernelWindowShellTileBackground(const char *);
int kernelWindowShellCenterBackground(const char *);
int kernelWindowShellRaiseWindowMenu(void);
kernelWindowComponent *kernelWindowShellNewTaskbarIcon(image *);
kernelWindowComponent *kernelWindowShellNewTaskbarTextLabel(const char *);
void kernelWindowShellDestroyTaskbarComp(kernelWindowComponent *);
kernelWindowComponent *kernelWindowShellIconify(kernelWindow *, int, image *);

// Functions for managing components.  This first batch is from
// kernelWindowComponent.c
kernelWindowComponent *kernelWindowComponentNew(objectKey,
	componentParameters *);
void kernelWindowComponentDestroy(kernelWindowComponent *);
int kernelWindowComponentSetCharSet(kernelWindowComponent *, const char *);
int kernelWindowComponentSetVisible(kernelWindowComponent *, int);
int kernelWindowComponentSetEnabled(kernelWindowComponent *, int);
int kernelWindowComponentGetWidth(kernelWindowComponent *);
int kernelWindowComponentSetWidth(kernelWindowComponent *, int);
int kernelWindowComponentGetHeight(kernelWindowComponent *);
int kernelWindowComponentSetHeight(kernelWindowComponent *, int);
int kernelWindowComponentFocus(kernelWindowComponent *);
int kernelWindowComponentUnfocus(kernelWindowComponent *);
int kernelWindowComponentDraw(kernelWindowComponent *);
int kernelWindowComponentGetData(kernelWindowComponent *, void *, int);
int kernelWindowComponentSetData(kernelWindowComponent *, void *, int, int);
int kernelWindowComponentGetSelected(kernelWindowComponent *, int *);
int kernelWindowComponentSetSelected(kernelWindowComponent *, int);

// Constructors exported by the different component types
kernelWindowComponent *kernelWindowNewBorder(objectKey, borderType,
	componentParameters *);
kernelWindowComponent *kernelWindowNewButton(objectKey, const char *,
	image *, componentParameters *);
kernelWindowComponent *kernelWindowNewCanvas(objectKey, int, int,
	componentParameters *);
kernelWindowComponent *kernelWindowNewCheckbox(objectKey, const char *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewContainer(objectKey, const char *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewDivider(objectKey, dividerType,
	componentParameters *);
kernelWindowComponent *kernelWindowNewIcon(objectKey, image *,
	const char *, componentParameters *);
kernelWindowComponent *kernelWindowNewImage(objectKey, image *, drawMode,
	componentParameters *);
kernelWindowComponent *kernelWindowNewList(objectKey, windowListType,
	int, int, int, listItemParameters *, int, componentParameters *);
kernelWindowComponent *kernelWindowNewListItem(objectKey, windowListType,
	listItemParameters *, componentParameters *);
kernelWindow *kernelWindowNewMenu(kernelWindow *, kernelWindowComponent *,
	const char *, windowMenuContents *, componentParameters *);
kernelWindowComponent *kernelWindowNewMenuBar(kernelWindow *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewMenuBarIcon(objectKey parent, image *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewMenuItem(kernelWindow *,
	const char *, componentParameters *);
kernelWindowComponent *kernelWindowNewPasswordField(objectKey, int,
	componentParameters *);
kernelWindowComponent *kernelWindowNewProgressBar(objectKey,
	componentParameters *);
kernelWindowComponent *kernelWindowNewRadioButton(objectKey, int, int,
	const char **, int,	componentParameters *);
kernelWindowComponent *kernelWindowNewScrollBar(objectKey, scrollBarType,
	int, int, componentParameters *);
kernelWindowComponent *kernelWindowNewSlider(objectKey, scrollBarType,
	int, int, componentParameters *);
kernelWindowComponent *kernelWindowNewSysContainer(kernelWindow *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewTextArea(objectKey, int, int, int,
	componentParameters *);
kernelWindowComponent *kernelWindowNewTextField(objectKey, int,
	componentParameters *);
kernelWindowComponent *kernelWindowNewTextLabel(objectKey, const char *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewTitleBar(kernelWindow *,
	componentParameters *);
kernelWindowComponent *kernelWindowNewTree(objectKey, windowTreeItem *, int,
	int, componentParameters *);

#define _KERNELWINDOW_H
#endif


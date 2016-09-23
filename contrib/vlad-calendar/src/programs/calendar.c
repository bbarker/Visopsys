#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/api.h>


static char *weekDay[] = { 
    "Mo", "Tu", "We", "Th", "Fr",  "Sa", "Su"
};

static char *monthName[] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

static int monthDay[12];
static int date, month, year, DayOfWeek;
static int graphics = 0;
static listItemParameters *calListParams = NULL;
static objectKey calWindow   = NULL;
static objectKey btn_plus_m  = NULL;
static objectKey btn_minus_m = NULL;
static objectKey btn_plus_y  = NULL;
static objectKey btn_minus_y = NULL;
static objectKey label_month = NULL;
static objectKey label_year  = NULL;
static objectKey list_cal    = NULL;


static void error(const char *format, ...)
{
  // Generic error message code for either text or graphics modes
  
  va_list list;
  char output[MAXSTRINGLENGTH];
  
  va_start(list, format);
  _expandFormatString(output, format, list);
  va_end(list);

  windowNewErrorDialog(calWindow, "Error", output);
}

static int leapYear(int year_)
{
    int status = 0;
    if (year_ % 4 == 0) return (status = 1);
	
    return (status);
}


static int getDays(int month_, int year_)
{
    monthDay[0]  = 31;
    monthDay[1]  = (leapYear(year_)) ? 29 : 28;
    monthDay[2]  = 31;
    monthDay[3]  = 30;
    monthDay[4]  = 31;
    monthDay[5]  = 30;
    monthDay[6]  = 31;
    monthDay[7]  = 31;
    monthDay[8]  = 30;
    monthDay[9]  = 31;
    monthDay[10] = 30;
    monthDay[11] = 31;
    
    return monthDay[month_];
}


static void textCalendar(void)
{    
    int days = getDays(month-1, year), i;    
    int firstDay  = rtcDayOfWeek(1, month, year);    
    int spaceSkip = 10 - (strlen(monthName[month-1]) + 5) / 2;

    for (i = 0; i < spaceSkip; i++) printf(" ");
    printf("%s %i", monthName[month-1], year);    

    printf("\n");
    for (i = 0; i < 7; i++) printf("%s ", weekDay[i]);

    printf("\n"); for (i = 0; i < firstDay; i++)
    printf("   ");
    
    for (i = 1; i <= days; i++)
    {
	DayOfWeek = rtcDayOfWeek(i, month, year);
	printf("%2i ", i);
	
	if (DayOfWeek == 6) printf("\n");
    }
    if (DayOfWeek != 6) printf("\n");
    
    return;
}


static void getUpdate(void)
{
    int i;
    int days = getDays(month-1, year);
    int firstDay  = rtcDayOfWeek(1, month, year);
    char year_num[5];
    
    for(i=0; i<49; i++)
	sprintf(calListParams[i].text, "  ");
    
    for (i=0; i<7; i++)
	sprintf(calListParams[i].text, "%s", weekDay[i]);
	
    for (i = 1; i <= days; i++)
	sprintf(calListParams[i+6+firstDay].text, "%2i", i);

    itoa(year, year_num);
    windowComponentSetData(list_cal, calListParams, 49);
    windowComponentSetData(label_month, monthName[month-1], 10);
    windowComponentSetData(label_year, year_num, 4);
	
    return;
}


static void eventHandler(objectKey key, windowEvent *event)
{    
    if ((key == calWindow) && (event->type == EVENT_WINDOW_CLOSE))
	windowGuiStop();

    if (event->type == EVENT_MOUSE_LEFTUP)
    {
	if (key == btn_minus_m) month = (month > 1)  ? month-1 : 12;
	if (key == btn_plus_m)  month = (month < 12) ? month+1 : 1;
	
	if (key == btn_minus_y) year = (year >= 1900) ? year-1 : 1900;
	if (key == btn_plus_y)  year = (year <= 3000) ? year+1 : 3000;
	
	if ((key == btn_minus_m) || (key == btn_plus_m) ||
	    (key == btn_minus_y) || (key == btn_plus_y))
	{
	    getUpdate();
	}
    }

    return;
}


static void constructWindow(void)
{
    componentParameters params;
    struct tm theTime;    
    
    bzero(&params, sizeof(componentParameters));
    calWindow = windowNew(multitaskerGetCurrentProcessId(), "Calendar");
    if (calWindow == NULL) exit(ERR_NOTINITIALIZED);
    
    params.gridWidth	= 1;
    params.gridHeight	= 1;
    params.padRight	= 1;
    params.padLeft	= 1;
    params.padTop	= 5;
    params.padBottom	= 5;
    params.orientationX	= orient_center;
    params.orientationY	= orient_middle;
    params.useDefaultForeground = 1;
    params.useDefaultBackground = 1;    
    btn_minus_m = windowNewButton(calWindow, "<", NULL, &params);
    windowRegisterEventHandler(btn_minus_m, &eventHandler);
    
    params.gridX = 1;
    btn_plus_m = windowNewButton(calWindow, ">", NULL, &params);
    windowRegisterEventHandler(btn_plus_m, &eventHandler);

    params.gridX = 2;
    label_month = windowNewTextLabel(calWindow, "", &params);
    windowComponentSetWidth(label_month, 80);
    
    params.gridX = 3;
    label_year = windowNewTextLabel(calWindow, "", &params);

    params.gridX = 4;
    btn_minus_y = windowNewButton(calWindow, "<", NULL, &params);
    windowRegisterEventHandler(btn_minus_y, &eventHandler);
    
    params.gridX = 5;
    btn_plus_y = windowNewButton(calWindow, ">", NULL, &params);
    windowRegisterEventHandler(btn_plus_y, &eventHandler);

    params.gridX = 0;
    params.gridY = 1;
    params.gridWidth = 6;
    params.fixedWidth = 1;
    getUpdate();
    list_cal = windowNewList(calWindow, windowlist_textonly, 7, 7, 0, calListParams, 49, &params);
    
    bzero(&theTime, sizeof(struct tm));
    rtcDateTime(&theTime);
    windowComponentSetSelected(list_cal, rtcDayOfWeek(1, month, year)+6+theTime.tm_mday);
    windowComponentFocus(list_cal);    
    windowRegisterEventHandler(calWindow, &eventHandler);
    
    // Make the window visible
    windowSetResizable(calWindow, 0);
    windowSetVisible(calWindow, 1);

    return;
}


static void graphCalendar(void)
{   
    calListParams = malloc(49 * sizeof(listItemParameters));
    if (calListParams == NULL)
    {
      error("Error getting memory");
      exit(ERR_MEMORY);
    }
     
    constructWindow();
    windowGuiRun();
    windowDestroy(calWindow);
    free(calListParams);
        
    return;    
}


static void usage(char *name)
{
    printf("usage:\n%s [-T]\n", name);
    return;
}


int main(int argc, char *argv[])
{
    int status = 0;    
    graphics = graphicsAreEnabled();
    
    if (argc == 2)
    {
//	if ((strcmp(argv[1], "-T") == 0) ||  (strcmp(argv[1], "-t") == 0))
	if (
	    (argv[1][0] == '-') &&  
	    ((argv[1][1] == 't') || (argv[1][1] == 'T')) &&
	    (argv[1][2] == '\0')
	    )
	    graphics = 0;
	else
	    {
		usage(argv[0]);
		return (status = ERR_ARGUMENTCOUNT);
	    }
    } else
	if (argc > 2)
	{
	    usage(argv[0]);
	    return (status = ERR_INVALID);
	}

    date  = rtcReadDayOfMonth();
    month = rtcReadMonth();
    year  = rtcReadYear();
	
    if (graphics) graphCalendar();
    else textCalendar();

    return (status);
}

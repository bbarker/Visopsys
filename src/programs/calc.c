/**
 * Visopsys Calculator
 *
 * written by Giuseppe Gatta
 *
 * This program is licensed under a two-clauses BSD license.
 */

/* This is the text that appears when a user requests help about this program
<help>

 -- calc --

A calculator program.

The button labelled "dec" changes the current numeric base, if you press it
once, the current numeric base will be hexadecimal and the button will now be
labelled "hex".  If you press it twice the base will be octal and the button
labelled "oct".  Pressing it three times restarts the cycle with "dec" and so
on.

Floating point behavior might look a bit strange to people not accustomed to
binary floating point operation: in fact, after typing some floating number,
you might see it gets turned into another number. This happens due to the
structure of binary floating pointer numbers.

Usage:
  calc

(Only available in graphics mode)

</help>
*/

#include <libintl.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/window.h>

#define _(string) gettext(string)

#define WINDOW_TITLE	_("Calculator")

enum {
	calc_op_divide,
	calc_op_multiply,
	calc_op_subtract,
	calc_op_add,
	calc_op_module,
	calc_op_pow,
	calc_op_result
};

static objectKey calculatorButtons[16];
static objectKey opButton[7];
static objectKey acButton;
static objectKey plminButton;
static objectKey ceButton;
static objectKey modeButton;
static objectKey floatButton;
static objectKey sqrtButton;
static objectKey factButton;

static const int modeButton_modes[3] = { 10, 16, 8 };
static int modeButton_pos = 0;
static objectKey result_label;
static char buttonName[2];
static objectKey window;
static volatile int program_exit = 0;
static int current_display_base;
static double number_field;
static char float_text[64];
static double calc_result = 0;
static int calc_first = 0;
static int calc_entered = 0;
static int calc_float = -1;
static int calc_float_pw = 10;
static int last_op;
static char number_text[256];


static void update_calculator_display(double number)
{
	int x;

	if (current_display_base == 10)
	{

		if (calc_float > 0)
		{
			_lnum2str(number, number_text, current_display_base, 1);
			strcat(number_text, ".");
			strcat(number_text, float_text);
		}
		else
		{
			_dbl2str(number, number_text, 3);

			for (x = strlen(number_text) - 1;number_text[x] == '0'; x--);

			if (number_text[x] == '.')
				number_text[x] = '\0';
			else
				number_text[x+1] = '\0';
		}
	}
	else
	{
		_lnum2str(number, number_text, current_display_base, 1);
	}

	windowComponentSetData(result_label, number_text, 32, 1 /* redraw */);
}


static void reset_calculator(void)
{
	number_field = 0;
	calc_result = 0;
	calc_first = 1;
	calc_entered = 0;
	calc_float = -1;
	last_op = calc_op_result;

	update_calculator_display(0);
}


static void switch_number_base(int new_base)
{
	int x;

	for (x = 0; x < 16; x++)
		windowComponentSetEnabled(calculatorButtons[x], x < new_base);

	current_display_base = new_base;

	switch(new_base)
	{
		case 8:
			strcpy(number_text, "oct");
		break;

		case 10:
			strcpy(number_text, "dec");
		break;

		case 16:
			strcpy(number_text, "hex");
		break;

		default:
			sprintf(number_text, "B%02d", current_display_base);
	}

	windowComponentSetData(modeButton, number_text, 3, 1 /* redraw */);
}


static void refreshWindow(void)
{
	// We got a 'window refresh' event (probably because of a language switch),
	// so we need to update things

	// Re-get the language setting
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("calc");

	// Re-get the character set
	if (getenv(ENV_CHARSET))
		windowSetCharSet(window, getenv(ENV_CHARSET));

	// Refresh the window title
	windowSetTitle(window, WINDOW_TITLE);
}


static void eventHandler(objectKey key, windowEvent *event)
{
	int x;

	for (x = 0; x < 16; x++)
	{
		if (key == calculatorButtons[x] && event->type == EVENT_MOUSE_LEFTUP)
		{
			if (last_op == calc_op_result)
				calc_first = 1;

			if (calc_float >= 0)
			{
				number_field += (double)x / calc_float_pw;
				calc_float_pw *= 10;

				if (calc_float < 63)
				{
					float_text[calc_float++] = x + '0';
					float_text[calc_float] = '\0';
				}
			}
			else
			{
				number_field *= current_display_base;
				number_field += x;
			}

			calc_entered = 1;

			update_calculator_display(number_field);
			return;
		}
	}

	for (x = 0; x < 7; x++)
	{
		if (key == opButton[x] && event->type == EVENT_MOUSE_LEFTUP)
		{
			if (calc_entered)
			{
				switch(last_op)
				{
					case calc_op_divide:
						if (!number_field)
						{
							windowNewErrorDialog(window, _("Division by zero"),
								_("Error: division by zero!"));
							reset_calculator();
							return;
						}
						calc_result /= number_field;
						break;

					case calc_op_multiply:
						calc_result *= number_field;
						break;

					case calc_op_subtract:
						calc_result -= number_field;
						break;

					case calc_op_add:
						calc_result += number_field;
						break;

					case calc_op_module:
						if (!number_field)
						{
							windowNewErrorDialog(window, _("Division by zero"),
								_("Error: division by zero!"));
							reset_calculator();
							return;
						}
						calc_result = fmod(calc_result, number_field);
						break;

					case calc_op_pow:
						calc_result = pow(calc_result, number_field);
						break;

					case calc_op_result:
						if (calc_first)
						{
							calc_result = number_field;
							calc_first = 0;
						}
						break;
				}

				number_field = 0;
				calc_entered = 0;
				calc_float = -1;

				update_calculator_display(calc_result);
			}

			last_op = x;
			return;
		}
	}

	if (key == acButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		reset_calculator();
	}

	else if (key == ceButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		number_field = 0;
		calc_entered = 0;
		windowComponentSetData(result_label, "0", 1, 1 /* redraw */);
	}

	else if (key == plminButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		double *number = calc_entered ? &number_field : &calc_result;

		if (*number)
		{
			*number *= -1;
			update_calculator_display(*number);
		}
	}

	else if (key == modeButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		modeButton_pos = (modeButton_pos + 1) % 3;
		switch_number_base(modeButton_modes[modeButton_pos]);

		update_calculator_display(calc_entered ? number_field : calc_result);
	}

	else if (key == floatButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		if (calc_float == -1 && current_display_base == 10)
		{
			calc_float = 0;
			calc_float_pw = 10;
			float_text[0] = '\0';
		}
	}

	else if (key == sqrtButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		double root = sqrt(calc_entered ? number_field : calc_result);

		reset_calculator();
		update_calculator_display((number_field = calc_result = root));
	}

	else if (key == factButton && event->type == EVENT_MOUSE_LEFTUP)
	{
		double fact = calc_entered ? number_field : calc_result;

		if (fact < 0)
		{
			windowNewErrorDialog(window, _("Invalid number"),
				_("Negative number!"));
			reset_calculator();
			return;
		}

		if (floor(fact) != fact)
		{
			windowNewErrorDialog(window, _("Invalid number"),
				_("Number is not integer!"));
			reset_calculator();
			return;
		}

		double factx;
		double factresult = 1;

		for (factx = 0; factx <= fact; factx++)
		{
			if (!factx)
				factresult = 1;
			else
				factresult *= factx;
		}

		reset_calculator();
		update_calculator_display((number_field = calc_result = factresult));
	}

	// Check for window events.
	else if (key == window)
	{
		// Check for window refresh
		if (event->type == EVENT_WINDOW_REFRESH)
			refreshWindow();

		// Check for the window being closed
		else if (event->type == EVENT_WINDOW_CLOSE)
		{
			program_exit = 1;
			windowGuiStop();
		}
	}
}


static void create_window(void)
{
	int x, y;

	componentParameters cparam;

	window = windowNew(multitaskerGetCurrentProcessId(), WINDOW_TITLE);

	memset(&cparam, 0, sizeof(componentParameters));

	cparam.gridWidth = 4;
	cparam.gridHeight = 1;
	cparam.orientationX = orient_left;
	cparam.orientationY = orient_top;

	result_label = windowNewTextLabel(window, "0", &cparam);

	memset(&cparam, 0, sizeof(componentParameters));

	for (y = 0, x = 7; y < 3; y++)
	{
		int o = x;

		cparam.gridY = y+1;
		cparam.gridWidth = 1;
		cparam.gridHeight = 1;
		cparam.padLeft = 0;
		cparam.padRight = 0;
		cparam.padTop = 0;
		cparam.padBottom = 0;
		cparam.flags = 0;
		cparam.orientationX = orient_left;
		cparam.orientationY = orient_top;

		for (; x < (o + 3); x++)
		{
			//printf("x = %d, y = %d\n", x, y);

			cparam.gridX = x - o;
			buttonName[0] = x + '0';
			buttonName[1] = '\0';
			calculatorButtons[x] =
				windowNewButton(window, buttonName, NULL, &cparam);
		}
		x = o - 3;
	}

	cparam.gridX = 4;
	cparam.gridY = 1;

	calculatorButtons[10] = windowNewButton(window, "A", NULL, &cparam);
	cparam.gridY++;

	calculatorButtons[11] =	windowNewButton(window, "B", NULL, &cparam);
	cparam.gridY++;

	calculatorButtons[12] =	windowNewButton(window, "C", NULL, &cparam);
	cparam.gridY++;

	calculatorButtons[13] =	windowNewButton(window, "D", NULL, &cparam);
	cparam.gridY++;

	calculatorButtons[14] =	windowNewButton(window, "E", NULL, &cparam);
	cparam.gridY++;

	calculatorButtons[15] =	windowNewButton(window, "F", NULL, &cparam);

	cparam.gridX = 0;
	cparam.gridY = 4;
	calculatorButtons[0] = windowNewButton(window, "0", NULL, &cparam);

	cparam.gridX++;
	opButton[calc_op_result] = windowNewButton(window, "=", NULL, &cparam);

	cparam.gridX++;
	plminButton = windowNewButton(window, "+/-", NULL, &cparam);

	cparam.gridX = 3;
	cparam.gridY = 1;
	opButton[calc_op_divide] = windowNewButton(window, "/", NULL, &cparam);
	cparam.gridY++;
	opButton[calc_op_multiply] = windowNewButton(window, "*", NULL, &cparam);
	cparam.gridY++;
	opButton[calc_op_subtract] = windowNewButton(window, "-", NULL, &cparam);
	cparam.gridY++;
	opButton[calc_op_add] = windowNewButton(window, "+", NULL, &cparam);
	cparam.gridY++;
	opButton[calc_op_module]  = windowNewButton(window, "MOD", NULL, &cparam);

	cparam.gridX = 0;
	cparam.gridY = 5;

	ceButton = windowNewButton(window, "CE", NULL, &cparam);

	cparam.gridX++;

	acButton = windowNewButton(window, "AC", NULL, &cparam);

	cparam.gridX++;

	modeButton = windowNewButton(window, "dec", NULL, &cparam);
	modeButton_pos = 0;
	switch_number_base(modeButton_modes[modeButton_pos]);

	cparam.gridX = 0;
	cparam.gridY = 6;

	floatButton = windowNewButton(window, ".", NULL, &cparam);

	cparam.gridX++;
	sqrtButton = windowNewButton(window, "sqrt", NULL, &cparam);

	cparam.gridX++;
	opButton[calc_op_pow] = windowNewButton(window, "pow", NULL, &cparam);

	cparam.gridX++;
	factButton = windowNewButton(window, "n!", NULL, &cparam);

	windowSetVisible(window, 1);

	windowRegisterEventHandler(window, eventHandler);

	for (x = 0; x < 7; x++)
		windowRegisterEventHandler(opButton[x], eventHandler);

	for (x = 0; x < 16; x++)
		windowRegisterEventHandler(calculatorButtons[x], eventHandler);

	windowRegisterEventHandler(plminButton, eventHandler);
	windowRegisterEventHandler(acButton, eventHandler);
	windowRegisterEventHandler(ceButton, eventHandler);
	windowRegisterEventHandler(modeButton, eventHandler);
	windowRegisterEventHandler(floatButton, eventHandler);
	windowRegisterEventHandler(sqrtButton, eventHandler);
	windowRegisterEventHandler(factButton, eventHandler);
}


int main(int argc __attribute__((unused)), char *argv[])
{
	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("calc");

	// Only work in graphics mode
	if (!graphicsAreEnabled())
	{
		printf(_("\nThe \"%s\" command only works in graphics mode\n"),
			argv[0]);
		return (ERR_NOTINITIALIZED);
	}

	create_window();
	reset_calculator();

	windowGuiRun();
	windowDestroy(window);

	return 0;
}


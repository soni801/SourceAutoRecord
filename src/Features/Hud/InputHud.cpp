#include "InputHud.hpp"

#include "Modules/Client.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Scheme.hpp"
#include "Modules/Surface.hpp"
#include "Modules/InputSystem.hpp"
#include "Utils/SDK.hpp"
#include "Variable.hpp"

#include <cstring>
#include <sstream>

Variable sar_ihud("sar_ihud", "0", 0, 1, "Enabled or disables movement inputs HUD of client.\n");
Variable sar_ihud_x("sar_ihud_x", "2", "X position of input HUD.\n", 0);
Variable sar_ihud_y("sar_ihud_y", "2", "Y position of input HUD.\n", 0);
Variable sar_ihud_grid_padding("sar_ihud_grid_padding", "2", 0, "Padding between grid squares of input HUD.\n");
Variable sar_ihud_grid_size("sar_ihud_grid_size", "60", 0, "Grid square size of input HUD.\n");

InputHud inputHud;

InputHud::InputHud()
	: Hud(HudType_InGame | HudType_LoadingScreen, true) {

	elements = {
		{"forward", false, IN_FORWARD},
		{"back", false, IN_BACK},
		{"moveleft", false, IN_MOVELEFT},
		{"moveright", false, IN_MOVERIGHT},
		{"jump", false, IN_JUMP},
		{"duck", false, IN_DUCK},
		{"use", false, IN_USE},
		{"attack", false, IN_ATTACK},
		{"attack2", false, IN_ATTACK2},
		{"movement", true, 0},
		{"angles", true, 1}
	};

	ApplyPreset("normal", true);
}

void InputHud::SetInputInfo(int slot, int buttonBits, Vector movement) {

	auto &info = this->inputInfo[slot];

	// this was supposed to ensures that the button press is visible for at least one frame
	// idk doesn't seem to make much difference lmfao
	if (info.awaitingFrameDraw) {
		info.buttonBits |= buttonBits;
	} else {
		info.buttonBits = buttonBits;
	}
	
	info.movement = movement;
	info.awaitingFrameDraw = true;
}

bool InputHud::ShouldDraw() {
	return sar_ihud.GetBool() && Hud::ShouldDraw();
}

void InputHud::Paint(int slot) {

	auto &inputInfo = this->inputInfo[slot];

	// update angles array
	if (inputInfo.awaitingFrameDraw) {
		inputInfo.angles[1] = inputInfo.angles[0];
		inputInfo.angles[0] = engine->GetAngles(engine->IsOrange() ? 0 : slot);
		inputInfo.awaitingFrameDraw = false;
	}

	// do the actual drawing
	auto hudX = PositionFromString(sar_ihud_x.GetString(), true);
	auto hudY = PositionFromString(sar_ihud_y.GetString(), false);

	auto btnSize = sar_ihud_grid_size.GetInt();
	auto btnPadding = sar_ihud_grid_padding.GetInt();

	for (auto &element : elements) {
		if (!element.enabled) continue;

		int eX = hudX + element.x * (btnSize + btnPadding);
		int eY = hudY + element.y * (btnSize + btnPadding);

		int eWidth = element.width * btnSize + std::max(0, element.width - 1) * btnPadding;
		int eHeight = element.height * btnSize + std::max(0, element.height -1) * btnPadding;

		if (element.isVector) { 
			//drawing movement and angles vector displays
			surface->DrawRect(element.background, eX, eY, eX + eWidth, eY + eHeight);

			int font = scheme->GetDefaultFont() + element.textFont;

			int fontHeight = font >= 0 ? surface->GetFontHeight(font) : 0;

			// trying some kind of responsiveness here and getting the smallest side
			const int joystickSize = std::min((int)(eHeight - fontHeight * 2.2), eWidth);
			int r = joystickSize / 2;

			int jX = eX + eWidth/2 - r;
			int jY = eY + eHeight/2 - r;

			Color linesColor1 = element.textHighlight;
			Color linesColor2 = linesColor1;
			linesColor2._color[3] /= 2;

			surface->DrawColoredLine(jX, jY, jX + joystickSize, jY, linesColor1);
			surface->DrawColoredLine(jX, jY, jX, jY + joystickSize, linesColor1);
			surface->DrawColoredLine(jX + joystickSize, jY, jX + joystickSize, jY + joystickSize, linesColor1);
			surface->DrawColoredLine(jX, jY + joystickSize, jX + joystickSize, jY + joystickSize, linesColor1);

			//surface->DrawFilledCircle(jX + r, jY + r, r, Color(0,0,0,40));
			surface->DrawCircle(jX + r, jY + r, r, linesColor1);

			surface->DrawColoredLine(jX, jY + r, jX + joystickSize, jY + r, linesColor2);
			surface->DrawColoredLine(jX + r, jY, jX + r, jY + joystickSize, linesColor2);

			Vector v, visV;
			if (element.type == 0) {
				// recalculate movement values into controller inputs
				v = inputInfo.movement;
				v.y /= cl_forwardspeed.GetFloat();
				v.x /= cl_sidespeed.GetFloat();
				visV = v;
				visV.y *= -1;
			} else {
				// calculating the difference between angles in two frames
				v = {
					inputInfo.angles[1].y - inputInfo.angles[0].y,
					inputInfo.angles[1].x - inputInfo.angles[0].x,
				};

				while (v.x < -180.0f) v.x += 360.0f;
				if (v.x > 180.0f) v.x -= 360.0f;

				// make lower range of inputs easier to notice
				if (v.Length() > 0) {
					visV = v.Normalize() * pow(v.Length() / 180.0f, 0.2);
					visV.y *= -1;
				}
			}

			Color pointerColor = element.highlight;
			Vector pointerPoint = {jX + r + r * visV.x, jY + r + r * visV.y};
			surface->DrawColoredLine(jX + r, jY + r, pointerPoint.x, pointerPoint.y, pointerColor);
			surface->DrawFilledCircle(pointerPoint.x, pointerPoint.y, 5, pointerColor);

			Color textColor = element.textColor;
			if (fontHeight > 0) {
				surface->DrawTxt(font, jX, jY + joystickSize + 2, textColor, "x:%.3f", v.x);
				surface->DrawTxt(font, jX + r, jY + joystickSize + 2, textColor, "y:%.3f", v.y);
				surface->DrawTxt(font, jX, jY - fontHeight, textColor, element.text.c_str(), v.x);
			}
		} else {
			// drawing normal buttons
			bool pressed = false;
			if (element.isNormalKey)
				pressed = inputSystem->IsKeyDown((ButtonCode_t)element.type);
			else
				pressed = inputInfo.buttonBits & element.type;

			surface->DrawRectAndCenterTxt(
				pressed ? element.highlight : element.background,
				eX, eY, eX + eWidth, eY + eHeight,
				scheme->GetDefaultFont() + element.textFont,
				pressed ? element.textHighlight : element.textColor,
				element.text.c_str()
			);
		}
	}

	// now that we've drawn pressed buttons, we can release them
}

bool InputHud::GetCurrentSize(int &xSize, int &ySize) {
	// getting the size in grid cells
	int gridWidth = 0;
	int gridHeight = 0;
	for (auto &element : elements) {
		if (!element.enabled)continue;
		gridWidth = std::max(gridWidth, element.x + element.width);
		gridHeight = std::max(gridHeight, element.y + element.height);
	}

	//transforming them into actual hud width and height
	auto btnSize = sar_ihud_grid_size.GetInt();
	auto btnPadding = sar_ihud_grid_padding.GetInt();

	xSize = gridWidth * btnSize + std::max(0, gridWidth - 1) * btnPadding;
	ySize = gridHeight * btnSize + std::max(0, gridHeight - 1) * btnPadding;

	return true;
}

InputHud::InputHudElement *InputHud::GetElementByName(std::string name) {
	for (int i = 0; i < elements.size(); i++) {
		if (elements[i].name.compare(name) == 0) {
			return &elements[i];
		}
	}

	return nullptr;
}

void InputHud::ModifyElementParam(std::string name, std::string parameter, std::string value) {
	// recursively handling the "all" name to apply changes to all of the elements
	if (name.compare("all") == 0) {
		for (auto &element : elements) {
			ModifyElementParam(element.name, parameter, value);
		}
	}

	auto *element = GetElementByName(name);
	if (!element) return;

	int valueInt = std::atoi(value.c_str());
	auto valueColor = Utils::GetColor(value.c_str(), false);

	// changing given parameter
	if (parameter.compare("enabled") == 0) {
		element->enabled = valueInt > 0;
	} else if (parameter.compare("background") == 0) {
		element->background = valueColor.value_or(element->background);
	} else if (parameter.compare("highlight") == 0) {
		element->highlight = valueColor.value_or(element->highlight);
	} else if (parameter.compare("font") == 0) {
		element->textFont = valueInt;
	} else if (parameter.compare("textcolor") == 0) {
		element->textColor = valueColor.value_or(element->textColor);
	} else if (parameter.compare("texthighlight") == 0) {
		element->textHighlight = valueColor.value_or(element->textHighlight);
	} else if (parameter.compare("text") == 0) {
		element->text = value;
	} else if (parameter.compare("x") == 0) {
		element->x = valueInt;
	} else if (parameter.compare("y") == 0) {
		element->y = valueInt;
	} else if (parameter.compare("width") == 0) {
		element->width = valueInt;
	} else if (parameter.compare("height") == 0) {
		element->height = valueInt;
	} else if (parameter.compare("pos") == 0) {
		int x, y, width, height;
		if (sscanf(value.c_str(), "%u %u %u %u", &x, &y, &width, &height) == 4) {
			element->x = x;
			element->y = y;
			element->width = width;
			element->height = height;
		} else if (sscanf(value.c_str(), "%u %u", &x, &y) == 2) {
			element->x = x;
			element->y = y;
		}
	}
}

void InputHud::ApplyPreset(const char* preset, bool start) {

#define PARAM(x, y, z) ModifyElementParam(x, y, z)
	if (!strcmp(preset, "normal") || !strcmp(preset, "normal_mouse")) {
		PARAM("all", "background", "0 0 0 200");
		PARAM("all", "textcolor", "255 255 255 255");
		PARAM("all", "highlight", "255 255 255 255");
		PARAM("all", "texthighlight", "255 255 255 255");
		PARAM("all", "font", "1");
		PARAM("all", "enabled", "1");

		PARAM("forward", "text", "W");
		PARAM("back", "text", "S");
		PARAM("moveleft", "text", "A");
		PARAM("moveright", "text", "D");
		PARAM("jump", "text", "Jump");
		PARAM("use", "text", "+use");
		PARAM("duck", "text", "Duck");
		PARAM("attack", "text", "LMB");
		PARAM("attack2", "text", "RMB");

		PARAM("forward", "pos", "2 0 1 1");
		PARAM("back", "pos", "2 1 1 1");
		PARAM("moveleft", "pos", "1 1 1 1");
		PARAM("moveright", "pos", "3 1 1 1");
		PARAM("jump", "pos", "1 2 3 1");
		PARAM("use", "pos", "3 0 1 1");
		PARAM("duck", "pos", "0 2 1 1");
		PARAM("attack", "pos", "4 2 1 1");
		PARAM("attack2", "pos", "5 2 1 1");

		PARAM("movement", "enabled", "0");
		PARAM("angles", "enabled", "0");

		if (!start) sar_ihud_grid_size.SetValue(60);

		if (!strcmp(preset, "normal_mouse")) {
			PARAM("forward", "pos", "1 0 1 1");
			PARAM("back", "pos", "1 1 1 1");
			PARAM("moveleft", "pos", "0 1 1 1");
			PARAM("moveright", "pos", "2 1 1 1");
			PARAM("use", "pos", "2 0 1 1");
			PARAM("jump", "pos", "1 2 2 1");
			PARAM("attack", "pos", "4 0 1 1");
			PARAM("attack2", "pos", "5 0 1 1");

			PARAM("angles", "enabled", "1");
			PARAM("angles", "pos", "4 1 2 2");
			PARAM("angles", "font", "-5");
			PARAM("angles", "texthighlight", "255 255 255 20");
		}
	} else if (!strcmp(preset, "tas")) {
		PARAM("all", "background", "0 0 0 200");
		PARAM("all", "textcolor", "255 255 255 255");
		PARAM("all", "highlight", "255 255 255 255");
		PARAM("all", "texthighlight", "255 255 255 255");
		PARAM("all", "font", "1");
		PARAM("all", "enabled", "1");

		PARAM("forward", "enabled", "0");
		PARAM("back", "enabled", "0");
		PARAM("moveleft", "enabled", "0");
		PARAM("moveright", "enabled", "0");

		PARAM("jump", "text", "Jump");
		PARAM("use", "text", "Use");
		PARAM("duck", "text", "Duck");
		PARAM("attack", "text", "Blue");
		PARAM("attack2", "text", "Orange");

		PARAM("movement", "pos", "0 0 5 5");
		PARAM("angles", "pos", "5 0 5 5");
		PARAM("duck", "pos", "0 5 2 1");
		PARAM("use", "pos", "2 5 2 1");
		PARAM("jump", "pos", "4 5 2 1");
		PARAM("attack", "pos", "6 5 2 1");
		PARAM("attack2", "pos", "8 5 2 1");

		PARAM("movement", "highlight", "255 150 0 255");
		PARAM("angles", "highlight", "0 150 255 255");
		PARAM("movement", "texthighlight", "255 200 100 100");
		PARAM("angles", "texthighlight", "100 200 255 100");
		PARAM("movement", "font", "13");
		PARAM("angles", "font", "13");
		PARAM("movement", "text", "move analog");
		PARAM("angles", "text", "view analog");

		if (!start) sar_ihud_grid_size.SetValue(40);
	} else {
		console->Print("Unknown input hud preset %s!\n", preset);
	}

#undef PARAM
}

bool InputHud::HasElement(const char* elementName) {
	bool elementExists = false;
	for (auto &element : elements) {
		if (element.name.compare(elementName) == 0) {
			elementExists = true;
			break;
		}
	}
	return elementExists;
}

bool InputHud::IsValidParameter(const char* param) {
	const char *validParams[] = {"enabled", "text", "font", "pos", "x", "y", "width", "height", "font", "background", "highlight", "textcolor", "texthighlight"};
	
	for (const char *validParam : validParams) {
		if (!strcmp(validParam, param)) {
			return true;
		}
	}
	return false;
}

std::string InputHud::GetParameterValue(std::string name, std::string parameter) {
	auto *element = GetElementByName(name);
	if (!element) return "";

	auto colorToString = [](Color color) -> std::string {
		std::stringstream stream;
		stream << "#" 
			<< std::hex << color.r() 
			<< std::hex << color.g() 
			<< std::hex << color.b()
			<< std::hex << color.a();
		return stream.str();
	};

	if (parameter.compare("enabled") == 0) {
		return element->enabled ? "1" : "0";
	} else if (parameter.compare("background") == 0) {
		return colorToString(element->background);
	} else if (parameter.compare("highlight") == 0) {
		return colorToString(element->highlight);
	} else if (parameter.compare("font") == 0) {
		return std::to_string(element->textFont);
	} else if (parameter.compare("textcolor") == 0) {
		return colorToString(element->textColor);
	} else if (parameter.compare("texthighlight") == 0) {
		return colorToString(element->textHighlight);
	} else if (parameter.compare("text") == 0) {
		return element->text;
	} else if (parameter.compare("x") == 0) {
		return std::to_string(element->x);
	} else if (parameter.compare("y") == 0) {
		return std::to_string(element->y);
	} else if (parameter.compare("width") == 0) {
		return std::to_string(element->width);
	} else if (parameter.compare("height") == 0) {
		return std::to_string(element->height);
	} else if (parameter.compare("pos") == 0) {
		return Utils::ssprintf("x: %u y: %u width: %u height: %u", element->x, element->y, element->width, element->height);
	} else {
		return "";
	}
}

void InputHud::AddElement(std::string name, int type) {
	char first = name.at(0);
	if (first >= 97)
		first -= 32;  // Convert to uppercase

	elements.push_back({
		name, false, type, true,				// name, isVector, type, isNormalKey
		true,									// enabled
		0, 0, 1, 1,								// x, y, width, height
		Color(0, 0, 0, 200),                    // background
		Color(255, 255, 255, 255),              // highlight
		std::string(1, first) + name.substr(1), // text
		1,                                      // font
		Color(255, 255, 255, 255),              // text color
		Color(255, 255, 255, 255),              // text highlight
	});
}


CON_COMMAND_COMPLETION(sar_ihud_preset, "sar_ihud_preset <preset> - modifies input hud based on given preset\n", ({"normal", "normal_mouse", "tas"})) {
	if (args.ArgC() != 2) {
		console->Print(sar_ihud_preset.ThisPtr()->m_pszHelpString);
		return;
	}

	const char *preset = args.Arg(1);
	inputHud.ApplyPreset(preset, false);
}

DECL_COMMAND_COMPLETION(sar_ihud_modify) {
	while (isspace(*match)) ++match;

	if (std::string(match).find(" ") != std::string::npos) {
		// we've probably started another arg; don't offer any completions
		return 0;
	}

	if (std::strstr("all", match)) items.push_back("all");

	for (auto &element : inputHud.elements) {
		if (items.size() == COMMAND_COMPLETION_MAXITEMS)
			break;

		if (std::strstr(element.name.c_str(), match)) {
			items.push_back(element.name);
		}
	}

	FINISH_COMMAND_COMPLETION();
}

CON_COMMAND_F_COMPLETION(sar_ihud_modify,
	"sar_ihud_modify <element|all> [param=value]... - modifies parameters in given element.\n"
    "Params: enabled, text, font, pos, x, y, width, height, font, background, highlight, textcolor, texthighlight.\n",
	0, sar_ihud_modify_CompletionFunc
) {
	if (args.ArgC() < 3) {
		console->Print(sar_ihud_modify.ThisPtr()->m_pszHelpString);
		return;
	}

	// checking if element exists
	const char *elementName = args[1];
	if (!inputHud.HasElement(elementName) && strcmp(elementName, "all")) {
		console->Print("Input HUD element %s doesn't exist.\n", elementName);
		//console->Print(sar_ihud_modify.ThisPtr()->m_pszHelpString);
		return;
	}

	std::vector<std::string> parameterOutput;

	auto addToParameterOutput = [&](std::string elementName, std::string parameter) {
		for (std::string &string : parameterOutput) {
			if (!Utils::StartsWith(string.c_str(), (elementName + ":").c_str())) continue;
			string += " " + parameter;
			return;
		}

		parameterOutput.push_back(std::string(elementName) + ": " + parameter);
	};

	// looping through every parameter
	for (int i = 2; i < args.ArgC(); i++) {
		std::string fullArg = args[i];
		auto separator = fullArg.find('=');
		if (separator == std::string::npos) {
			if (!inputHud.IsValidParameter(fullArg.c_str())) continue;

			// recursively handling the "all" name to print out the parameter for all elements
			if (std::string(elementName).compare("all") == 0) {
				for (auto &element : inputHud.elements) {
					addToParameterOutput(element.name, fullArg + "=" + inputHud.GetParameterValue(element.name, fullArg.c_str()));
				}
				continue;
			}

			// Print the value of the parameter
			auto parameterValue = inputHud.GetParameterValue(std::string(elementName), fullArg.c_str());
			if (parameterValue.empty()) continue;
			addToParameterOutput(elementName, fullArg + "=" + parameterValue);
			continue;
		}

		std::string param = fullArg.substr(0, separator);
		std::string value = fullArg.substr(separator + 1);

		if (inputHud.IsValidParameter(param.c_str())) {
			inputHud.ModifyElementParam(elementName, param, value);
		} else {
			console->Print("Unknown input HUD parameter %s.\n", param.c_str());
		}
	}

	if (!parameterOutput.empty()) {
		for (std::string &string : parameterOutput) {
			console->Print("%s\n", string.c_str());
		}
	}
}

CON_COMMAND(sar_ihud_add_key, "sar_ihud_add_key <key>") {
	if (args.ArgC() < 2) {
		console->Print(sar_ihud_add_key.ThisPtr()->m_pszHelpString);
		return;
	}

	if (inputHud.HasElement(args[1])) {
		console->Print("Input HUD already has this key.\n");
		return;
	}

	int keyCode = inputSystem->GetButton(args[1]);
	if (keyCode == -1) {
		console->Print("Key %s does not exist.\n", args[1]);
		return;
	}

	inputHud.AddElement(args[1], keyCode);
}

CON_COMMAND_HUD_SETPOS(sar_ihud, "input HUD")

#include <iostream>
#include <thread>
#include <atomic>
#include <SDL2/SDL.h>
#include "Application.h"
#include "util/Statistics.h"
#include "UserInput.h"
#include "View.h"
#include "v8datamodel/BaseRenderJob.h"
#include "RenderSettingsItem.h"
#include "VirtualizerSDK.h"
#include "StealthCodeArea.h"

#ifdef EMSCRIPTEN
#include "emscripten.h"
#include "v8datamodel/DataModel.h"
#include "v8datamodel/DataModelJob.h"
#include "util/ContentId.h"
#include "security/SecurityContext.h"
#include "v8datamodel/ContentProvider.h"
#include "network/Players.h"
#include "network/Player.h"
#include "util/RunStateOwner.h"
#include "v8datamodel/StarterPlayerService.h"
#include "v8datamodel/PlayerScripts.h"
#include "v8datamodel/ModelInstance.h"
#include "script/script.h"
#include "script/ScriptContext.h"
#include "util/ProtectedString.h"
#include "network/GameConfigurer.h"
#include <fstream>
#include <sstream>
#include <boost/thread.hpp>
#endif

int stealthVar = 0;
std::atomic<bool> isRunning(true);

boost::scoped_ptr<RBX::UserInput> input;
boost::shared_ptr<RBX::View> view;
static SDL_Event e;

#ifdef EMSCRIPTEN
// Keep the PlayerConfigurer alive for the lifetime of the multiplayer session.
// It owns all network connection signal handlers — letting it die disconnects them.
static boost::shared_ptr<RBX::PlayerConfigurer> g_playerConfigurer;
#endif

#ifdef EMSCRIPTEN
// Runs on the main DataModel thread (via submitTask) after place load completes.
// Must be on the main thread so that signals (camera, character spawn) propagate correctly.
static void postLoadSetup(RBX::DataModel* dm)
{
	RBX::Security::Impersonator impersonate(RBX::Security::RobloxGameScript_);

	// Create a LocalPlayer so the engine enters VISIT_SOLO mode instead of EDIT mode.
	// getGameMode() returns EDIT when no LocalPlayer exists; VISIT_SOLO when one does.
	if (RBX::Network::Players* players = RBX::ServiceProvider::create<RBX::Network::Players>(dm))
	{
		if (!RBX::Network::Players::findLocalPlayer(dm))
		{
			players->createLocalPlayer(1, false);
			RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO, "Created LocalPlayer (userId=1)");
		}
	}

	// Inject our custom Animate script into StarterCharacterScripts BEFORE
	// luaLoadCharacter so Player::loadCharacter() sees the override and skips
	// the rbxm version that tries to fetch animations from www.roblox.com.
	if (RBX::StarterPlayerService* sps = RBX::ServiceProvider::create<RBX::StarterPlayerService>(dm))
	{
		RBX::StarterCharacterScripts* scs =
			sps->findFirstChildOfType<RBX::StarterCharacterScripts>();
		if (!scs)
		{
			boost::shared_ptr<RBX::StarterCharacterScripts> newScs =
				RBX::Creatable<RBX::Instance>::create<RBX::StarterCharacterScripts>();
			newScs->setName("StarterCharacterScripts");
			newScs->setParent(sps);
			scs = newScs.get();
		}

		if (scs && !scs->findFirstChildByName("Animate"))
		{
			std::string animFile = RBX::ContentProvider::assetFolder() +
			                       "scripts/CharacterScripts/Animate.lua";
			std::ifstream in(animFile, std::ios::in | std::ios::binary);
			if (in)
			{
				std::stringstream ss;
				ss << in.rdbuf();
				boost::shared_ptr<RBX::LocalScript> animScript =
					RBX::Creatable<RBX::Instance>::create<RBX::LocalScript>();
				animScript->setName("Animate");
				animScript->setEmbeddedCode(RBX::ProtectedString::fromTrustedSource(ss.str()));
				animScript->setParent(scs);
				RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO,
					"Injected custom Animate script from %s", animFile.c_str());
			}
			else
			{
				RBX::StandardOut::singleton()->printf(RBX::MESSAGE_WARNING,
					"Could not open %s — falling back to rbxm Animate", animFile.c_str());
			}
		}
	}

	// Spawn the character and fire gameLoaded() so the engine fully initializes
	// for play mode (camera follows character, movement scripts activate, etc.)
	if (RBX::Network::Player* player = RBX::Network::Players::findLocalPlayer(dm))
	{
		try {
			player->luaLoadCharacter(true);
			RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO, "luaLoadCharacter OK");
		} catch (const std::exception& ex) {
			RBX::StandardOut::singleton()->printf(RBX::MESSAGE_ERROR, "luaLoadCharacter failed: %s", ex.what());
		}

		// Direct Animate injection: bypass the StarterCharacterScripts clone path
		// entirely by parenting our LocalScript straight into the character model.
		// This is the most reliable approach — no clone, no security restriction.
		if (RBX::ModelInstance* character = player->getCharacter())
		{
			// Remove whatever Animate was loaded (rbxm version from else-branch or
			// any previously injected copy) so we don't get double-scripts.
			if (RBX::Instance* old = character->findFirstChildByName("Animate"))
			{
				old->destroy();
				RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO, "Removed existing Animate script");
			}

			std::string animFile = RBX::ContentProvider::assetFolder() +
			                       "scripts/CharacterScripts/Animate.lua";
			std::ifstream animIn(animFile, std::ios::in | std::ios::binary);
			if (animIn)
			{
				std::stringstream ss;
				ss << animIn.rdbuf();
				boost::shared_ptr<RBX::LocalScript> animScript =
					RBX::Creatable<RBX::Instance>::create<RBX::LocalScript>();
				animScript->setName("Animate");
				animScript->setEmbeddedCode(RBX::ProtectedString::fromTrustedSource(ss.str()));
				animScript->setParent(character);
				RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO,
					"Injected Animate directly into character model");
			}
			else
			{
				RBX::StandardOut::singleton()->printf(RBX::MESSAGE_WARNING,
					"Could not open %s for direct character injection", animFile.c_str());
			}
		}
		else
		{
			RBX::StandardOut::singleton()->printf(RBX::MESSAGE_WARNING,
				"Character is null after luaLoadCharacter — direct Animate injection skipped");
		}
	}

	// Transition RunService to RS_RUNNING so that camera/control scripts load
	// and physics starts. StarterPlayerScripts::InitializeDefaultScripts() waits
	// for this transition (via runTransitionSignal) before loading characterCameraScript
	// and characterControlScript. Without this call the game stays in RS_STOPPED
	// (editor mode) and movement/camera never initialize.
	if (RBX::RunService* runService = RBX::ServiceProvider::create<RBX::RunService>(dm))
	{
		runService->run();
		RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO, "RunService::run() called -> RS_RUNNING");
	}

	// RobloxLoadingGui is intentionally left alive — the engine's own CoreScript
	// system manages the loading screen lifecycle (it fires game.Loaded when
	// content is ready and the Lua loading scripts hide the GUI themselves).
	// We must not force-destroy it here; doing so removes the loading screen
	// before Roblox's own transition logic runs.
	RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO,
		"postLoadSetup complete — RobloxLoadingGui left for engine to manage");
}

static void loadPlaceTaskSafe(boost::shared_ptr<RBX::DataModel> dm, std::string url)
{
	// Place loading requires RobloxScript-level identity to deserialize
	// Roblox-owned objects (CoreScripts, services, etc.)
	RBX::Security::Impersonator impersonate(RBX::Security::RobloxGameScript_);

	// Set the base URL so rbxassetid:// URIs resolve to real HTTP asset URLs.
	if (RBX::ContentProvider* cp = RBX::ServiceProvider::create<RBX::ContentProvider>(dm.get()))
		cp->setBaseUrl("https://www.roblox.com");

	try {
		// loadContent blocks on HTTP fetch — safe on background thread.
		dm->loadContent(RBX::ContentId(url));

		// Schedule post-load setup on the main DataModel thread so signals
		// (LocalPlayer creation, character spawn, camera switch) propagate correctly.
		dm->submitTask(boost::bind(&postLoadSetup, dm.get()), RBX::DataModelJob::Write);

	} catch (const std::exception& e) {
		RBX::StandardOut::singleton()->printf(RBX::MESSAGE_ERROR,
			"Failed to load place from '%s': %s", url.c_str(), e.what());
	} catch (...) {
		RBX::StandardOut::singleton()->printf(RBX::MESSAGE_ERROR,
			"Failed to load place from '%s': unknown error", url.c_str());
	}
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE
	void notifyPointerLockLost()
	{
		if (input)
		{
			input->forceMouseUnlock();
		}
	}

	EMSCRIPTEN_KEEPALIVE
	void loadPlaceFromUrl(const char* url)
	{
		if (!view) return;
		boost::shared_ptr<RBX::DataModel> dataModel = view->getDataModel();
		if (!dataModel) return;
		std::string urlStr(url);
		// Run on a detached background thread so synchronous HTTP fetches inside
		// loadContent don't block the main thread / browser event loop.
		// Pass shared_ptr so the DataModel stays alive for the submitTask call.
		boost::thread(boost::bind(&loadPlaceTaskSafe, dataModel, urlStr)).detach();
	}

	// Join a multiplayer game via PlayerConfigurer.
	// argsJson is a JSON object with the join parameters (see shell.html for format).
	// The WebSocketAddress param must point to the ws_bridge.py server that bridges
	// browser WebSocket connections to the native RakNet/RCCService UDP port.
	EMSCRIPTEN_KEEPALIVE
	void joinMultiplayerGame(const char* argsJson)
	{
		if (!view) return;
		boost::shared_ptr<RBX::DataModel> dm = view->getDataModel();
		if (!dm) return;

		std::string args(argsJson);

		// Allocate a new configurer and keep it alive globally.
		// The previous one (if any) is released here — this disconnects its
		// network signal handlers and ends the old session cleanly.
		boost::shared_ptr<RBX::PlayerConfigurer> configurer(new RBX::PlayerConfigurer());
		g_playerConfigurer = configurer;

		// configure() must run on the DataModel write thread so that all
		// Instance mutations (creating Network::Client, Players, etc.) are safe.
		// submitTask passes the DataModel* as the first argument to the functor.
		dm->submitTask([configurer, args](RBX::DataModel* taskDm) {
			configurer->configure(RBX::Security::LocalGUI_, taskDm, args);
		}, RBX::DataModelJob::Write);

		RBX::StandardOut::singleton()->printf(RBX::MESSAGE_INFO,
			"[joinMultiplayerGame] Submitted PlayerConfigurer::configure() to DataModel");
	}

	// Add a second (bot) player to the Players service so games that require
	// >= 2 players to start can proceed.  Parenting a Player with userId=0 and
	// Name="Player" to Players triggers onChildAdded, which (in solo/backend
	// mode) auto-assigns a unique negative userId, fires PlayerAdded, and bumps
	// NumPlayers — exactly what lobby scripts check.
	EMSCRIPTEN_KEEPALIVE
	void spawnBotPlayer()
	{
		if (!view) return;
		boost::shared_ptr<RBX::DataModel> dm = view->getDataModel();
		if (!dm) return;

		dm->submitTask([](RBX::DataModel* taskDm) {
			RBX::Security::Impersonator impersonate(RBX::Security::RobloxGameScript_);

			RBX::Network::Players* players =
				RBX::ServiceProvider::create<RBX::Network::Players>(taskDm);
			if (!players || players->getNumPlayers() >= 2) return;

			// Create the bot via Lua so Players.ChildAdded fires on the Lua side.
			// Direct C++ setParent on Network::Player bypasses the Lua reflection layer
			// and the leaderboard never sees the player.
			RBX::ScriptContext* sc = RBX::ServiceProvider::create<RBX::ScriptContext>(taskDm);
			if (!sc) return;

			sc->executeInNewThread(
				RBX::Security::RobloxGameScript_,
				RBX::ProtectedString::fromTrustedSource(
					"local Players = game:GetService('Players')\n"
					"if #Players:GetPlayers() < 2 then\n"
					"    local p = Instance.new('Player')\n"
					"    p.Name = 'Bot'\n"
					"    p.Parent = Players\n"
					"    print('[spawnBotPlayer] Bot added via Lua, count=' .. #Players:GetPlayers())\n"
					"end\n"
				),
				"SpawnBotPlayer"
			);
		}, RBX::DataModelJob::Write);
	}
}
#endif

void main_loop()
{
	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
		case SDL_QUIT:
			isRunning = false;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
		case SDL_MOUSEMOTION:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEWHEEL:
			input->postUserInputMessage(&e);
			break;
		}
	}

	view->GetMarshaller()->ProcessJobs();
}

STEALTH_AUX_FUNCTION
void stealth_area(void)
{
	STEALTH_AREA_START
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_CHUNK
	STEALTH_AREA_END
}

int main(int argc, char *argv[])
{
	if (stealthVar == 0x11223344)
	{
		stealth_area();
	}

	std::cout << "Starting UniversalClient..." << std::endl;

	std::cout << "[DBG] SDL_Init..." << std::endl;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
	{
		std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
		return 1;
	}
	std::cout << "[DBG] SDL_Init OK" << std::endl;

	SDL_Window *window = SDL_CreateWindow(
		"ROBLOX",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		800,
		600,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	SDL_ShowCursor(SDL_DISABLE);
	if (!window)
	{
		std::cerr << "Failed to create SDL window: " << SDL_GetError() << "\n";
		SDL_Quit();
		return 1;
	}
	std::cout << "[DBG] SDL window OK" << std::endl;

	RBX::Application app;
	std::cout << "[DBG] ParseArguments..." << std::endl;
	if (!app.ParseArguments(window, argc, argv))
	{
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	std::cout << "[DBG] ParseArguments OK" << std::endl;

	std::cout << "[DBG] Initialize..." << std::endl;
	view = app.Initialize(window);
	if (!view)
	{
		std::cerr << "Failed to initialize app\n";
		return 0;
	}
	std::cout << "[DBG] Initialize OK" << std::endl;

	RBX::Name::onStaticInitDone();

	input.reset(new RBX::UserInput(window, app.getPreloadedGame()));
	std::cout << "[DBG] Input OK, starting main loop..." << std::endl;

#ifdef EMSCRIPTEN
	emscripten_set_main_loop(main_loop, 0, true);
#else
	while (isRunning)
	{
		main_loop();
	}
	app.Shutdown();
	SDL_DestroyWindow(window);
	SDL_Quit();
#endif

	return 0;
}
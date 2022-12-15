# cs1230-final-project
Alexander Gaidis and Neophytos Christou's final project for Brown University's CS 1230: Introduction to Computer Graphics.

# How to use 

Note: the hacks were only tested on 64-bit Debian 11. They won't work in any non 64-bit Linux OSes, and might not even work on other distros, since we rely on hardcoded offsets.

## Compilation

The main wallhack binary and the shared library to be preloaded can be compiled using the provided Makefiles in `wall-hack` and `drawing`.

## Preloading the shared library

The produced shared object (`drawing/libdraw.so`) needs to be preloaded into the CSGO binary. To do this, right click on `Counter-Strike: Global Offensive` in the Steam library, click `Properties` -> `General` and type the following under `LAUNCH OPTIONS`: `LD_PRELOAD="$LD_PRELOAD=/path/to/libdraw.so" %command%`, replacing the path with the full path to the compiled shared library.

## Using the hacks

After a game has loaded, run the `main` binary under `wall-hack`. The program will expose a command line which can be used to turn the hacks on and off by typing the hack's identifier followed by `1` to turn it on or `0` to turn it off. The three supported hacks are `glow`, `wireframe` and `healthbars` (i.e., type `glow 1` to turn on the glows, and so on).

## Other notes

The file `wall-hack/list.h` implements a linked-list in C. This file was used verbatim from some of the stencil code we received in cs1680 (i.e., we did not write it).

# Proposal

We are very interested in offensive and defensive software security, and thus we would like to explore its intersection with graphics. Specifically, we plan to hack the game client of a first-person shooter (FPS) so the player is able to see enemies through walls (i.e., "wall hacks") and, if time permits, replace the textures of players with colors to easily identify teammates and enemies (i.e., "cham hacks" or "chameleon hacks"). 

Many games utilize API calls to local graphics libraries such as OpenGL (https://www.pcgamingwiki.com/wiki/List_of_OpenGL_games) to handle rendering of objects and players. Our job in this project would be: (1) reverse engineer the game client to discover what calls are made to OpenGL and how they are used to render players and other objects, (2) write software that hooks these API function calls, allowing us to interpose on their functionality at run time, and (3) write code that runs at our installed hooks that manipulates the rendering of the player's screen to achieve the desired hack.

We are currently deciding which game would be best to target. However, here are a few choices we have come up with: Counter-Strike: Global Offensive, Doom (2016 or earlier), Star Wars: Republic Commando, Quake 4, etc. After this, we will begin reverse engineering which we anticipate will take one week (optimistically). Finally, we will write the exploit code which we anticipate will take one to two weeks. Both of us plan to work on each part together---an equal division of labor on each task. The final deliverable will be (in-part) a video of us playing the game against each other (on LAN), exhibiting our modifications to the game.

If we notice that we are running out of time and will not be able to implement the specific hacks mentioned above, we can opt for something simpler such as using our hooking mechanism to display things in the scene (such as a box around the sight reticle, etc.) so we still have a visually cool demo at the end.

# Notes

- [?] `CBaseEntity` is responsible for managing the data of player objects.
- [?] `client_panorama_client.so` is the shared object that manages players in game.
- Start on command line: `steam steam://rungameid/730`
- Been using `frida-trace` to trace program and search for calls to OpenGL libraries (looked for calls having *gl* in the name, thus far we have only found a few)
- Lots of calls to `libcairo_client.so` -- we looked this up and it turns out libcairo (cairographics.org) is a graphics library which provides high-level wrappers around lower-level APIs (e.g. OpenGL). We got its source code and figured out it makes call to OpenGL APIs but for some reason they are not visible in the trace.

- Found player (not enemy) position by finding health and calculating the offset to the position
- Found enemy position by walking up to enemy and having enemy move and searching for range of position values

## Design Check

### Final flow of the program

There are two options:
  
  - Use dynamic instrumentation framework to hook calls to library functions that render enemy models and replace them with our own.
  - Directly write to memory to modify the data representing the enemy model such that it is always rendered.
  
 ### Rough plan
 
  - Figure out where in memory the game stores data relating to enemy models (__currently doing this__).
  - Figure out what library calls write to / read from this memory addresses. This should hopefully give us the function that reads this data and renders it, and another higher-level function that calls the rendering function if the enemy is visible.
  - Figure out a way to always call the rendering function regardless of whether the enemy should be rendered or not. Alternatively, insert an extra function call to our own rendering function.
  - If that doesn't work, try to figure out a way to write in memory such that the enemy models are always visible.


# More notes

- Using Ghidra (static analysis)
    - Find string `"EntityGlowEffects"`
    - Find references to the aforementioned string to uncover `CGlowObjectManager::RenderGlowEffects()`
    - According the the System V AMD64 ABI, in C++ `this` is an implicit first parameter. Thus, we find references (calls) to `RenderGlowEffects()` to uncover `DoPostScreenSpaceEffects()` and look for the first argument to `RenderGlowEffects()`.
    - This reveals the first argument is the result of the function call `GlowObjectManager()` which has a static pointer to `CGlowObjectManager` at offset `0x2c9bf80` (the `.bss` section in `client_client.so`).


- NOTE: the addresses in ghidra seem to be 0x100000 off...? ---> yes this seems to be true...

# Turn on glow

- Find GlowObjectDefinition_t struct of player in memory (dereference s_GlowObjectManager, iterate through m_GlowObjectDefinitions -> currently manually cross-checking which object in list corresponds to the player by locating player entity in memory using PINCE and locating their health field -- need to find a way to get the right object in list automatically).
- Change GlowObjectDefinition_t fields:
```
GlowObjectDefinition
    0x00 - 0x04 = m_nNextFreeSlot
    0x08 - 0x10 = m_pEntity
    0x10 - 0x14 = m_vGlowColorX }
    0x14 - 0x18 = m_vGlowColorY } -> Set these to the color of the glow (each value is 0.0 to 1.0)
    0x18 - 0x1c = m_vGlowColorZ }
    0x1c - 0x20 = m_vGlowAlpha -> Set this to 1.0
    0x20 - 0x24 = m_bGlowAlphaCappedByRenderAlpha + padding??
    0x24 - 0x28 = m_flGlowAlphaFunctionOfMaxVelocity
    0x28 - 0x2c = m_flGlowAlphaMax -> Set this to 1.0 (usually already set)
    0x2c - 0x30 = m_flGlowPulseOverdrive
    0x30 - 0x31 = m_renderWhenOccluded -> Set this to 1
    0x31 - 0x32 = m_renderWhenUnoccluded
    0x38 - 0x3c = m_nRenderStyle
    0x3c - 0x40 = m_nSplitScreenSlot
```

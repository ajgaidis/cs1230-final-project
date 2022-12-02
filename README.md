# cs1230-final-project
Alexander Gaidis and Neophytos Christou's final project for Brown University's CS 1230: Introduction to Computer Graphics.

We are very interested in offensive and defensive software security, and thus we would like to explore its intersection with graphics. Specifically, we plan to hack the game client of a first-person shooter (FPS) so the player is able to see enemies through walls (i.e., "wall hacks") and, if time permits, replace the textures of players with colors to easily identify teammates and enemies (i.e., "cham hacks" or "chameleon hacks"). 

Many games utilize API calls to local graphics libraries such as OpenGL (https://www.pcgamingwiki.com/wiki/List_of_OpenGL_games) to handle rendering of objects and players. Our job in this project would be: (1) reverse engineer the game client to discover what calls are made to OpenGL and how they are used to render players and other objects, (2) write software that hooks these API function calls, allowing us to interpose on their functionality at run time, and (3) write code that runs at our installed hooks that manipulates the rendering of the player's screen to achieve the desired hack.

We are currently deciding which game would be best to target. However, here are a few choices we have come up with: Counter-Strike: Global Offensive, Doom (2016 or earlier), Star Wars: Republic Commando, Quake 4, etc. After this, we will begin reverse engineering which we anticipate will take one week (optimistically). Finally, we will write the exploit code which we anticipate will take one to two weeks. Both of us plan to work on each part together---an equal division of labor on each task. The final deliverable will be (in-part) a video of us playing the game against each other (on LAN), exhibiting our modifications to the game.

If we notice that we are running out of time and will not be able to implement the specific hacks mentioned above, we can opt for something simpler such as using our hooking mechanism to display things in the scene (such as a box around the sight reticle, etc.) so we still have a visually cool demo at the end.


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

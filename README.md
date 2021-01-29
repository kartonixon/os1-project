# OS1 - Room Management Game

The final project for Operating Systems 1 class @ MiNI PW. The goal of this project was to learn how to use POSIX concepts like signals, threads and mutexes in practice. The project can be compiled and ran only on POSIX compliant operating systems.

## Getting started 🛠

After cloning the repository in order to build an executable you can use the prepared Makefile

```sh
make all
```

Then you will be able to run the program

```sh
./rmg
```

## Coolest features 😎

### Maps

Every map is a connected graph. Each vertex is a separate room with a unique ID.

In this game, there are two ways of generating new maps

You can create a map from a directory tree using `map-from-dir-tree` command in the main menu. Every directory is a separate room. Rooms are connected with their parent directories and subdirectories.

You can also use `generate-random-map` to generate a random connected graph. The connectivity of a graph is checked with a help of an BFS algorithm.

### Items

Every room contains at most two items. Player also can hold only two items. Each item has a unique ID and a destination room ID. The goal of the game is to deliver each item to its destination while obeying the rules of the game.

Each game is started in parallel with a separate thread waiting for `SIGUSR1` signal. When `SIGUSR1` is delivered, the thread swaps current location of two randomly chosen items in the game. You can test it by using `sigusr1` command while playing the game.

### Autosave

Each game is started in parallel with an autosave thread. If the time from the last manual save or autosave exceeds 60 seconds, the current game state is saved to a file in the autosave path, by default `.game-autosave`

You can set the custom autosave path in two ways:

1. you can run the executable with an optional argument `-b <autosave-path>`
2. you can set the environmental variable `$GAME_AUTOSAVE`

## Final thoughts 🧠

Even if you manage to deliver every item to its destination, nothing happens. The game **never** ends, so you play as much as you want! Just don't forget to have a break sometimes and do something else.

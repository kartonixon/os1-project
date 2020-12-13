#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <math.h>
#include <ftw.h>
#include <dirent.h>

#define MAX_INPUT_LENGTH 256
#define MAX_QUEUE_SIZE 256
#define MAX_PATHFINDING_THREADS 100
#define MAX_FD 20
#define MIN_VERTEX_COUNT 4
#define MAX_VERTEX_COUNT 512

#define ELAPSED(start,end) ((end).tv_sec-(start).tv_sec)+(((end).tv_nsec - (start).tv_nsec) * 1.0e-9)
#define ERR(source) (perror(source),\
                     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     exit(EXIT_FAILURE))

// 
// STRUCTS
// 

typedef struct Item {
    int id;
    int dest_vertex_id;
} Item;

typedef struct AdjVertexNode {
    int id;
    struct AdjVertexNode* next;
} AdjVertexNode;

typedef struct Vertex {
    int id;
    AdjVertexNode* head;
    Item items[2];
    int assigned_item_ids[2];
} Vertex;

typedef struct Player {
    int location;
    Item items[2];
} Player;

typedef struct Queue {
    int items[MAX_QUEUE_SIZE];
    int front;
    int rear;
} Queue;

typedef struct Graph {
    int vertex_count;
    Vertex* vertices;
} Graph;

typedef struct Game {
    Graph* map;
    Player* player;
    struct timespec last_saved;
} Game;

typedef struct msp_node {
    int room_id;
    struct msp_node* next;
} msp_node;

typedef struct thread_pathfinder {
    pthread_t thread_id;
    Game* game_state;
    int room_id;
    int seed;
} thread_pathfinder;

typedef struct thread_autosave {
    pthread_t thread_id;
    Game* game_state;
    pthread_mutex_t* pmxGameState;
    char* path;
} thread_autosave;

typedef struct thread_signal {
    pthread_t thread_id;
    Game* game_state;
    pthread_mutex_t* pmxGameState;
} thread_signal;

// 
// BUFFER MANIPULATION FUNCTIONS
// 

void int_to_buffer(char* buffer, int i) {
    sprintf(&buffer[strlen(buffer)], "%4d", i);
}

void string_to_buffer(char* buffer, char* s) {
    sprintf(&buffer[strlen(buffer)], "%s", s);
}

void endline_to_buffer(char* buffer) {
    sprintf(&buffer[strlen(buffer)], "\n");
}

// 
// END OF BUFFER MANIPULATION FUNCTIONS
// 

// 
// QUEUE FUNCTIONS
// 

Queue* create_queue() {
    Queue* q = malloc(sizeof(Queue));
    if (q==NULL) ERR("malloc");
    q->front = -1;
    q->rear = -1;
    return q;
}

int is_empty(Queue* q) {
    if (q->rear == -1)
        return 1;
    else
        return 0;
}

void enqueue(Queue* q, int value) {
    if (q->rear == MAX_QUEUE_SIZE - 1);
    else {
        if (q->front == -1)
            q->front = 0;
        q->rear++;
        q->items[q->rear] = value;
    }
}

int dequeue(Queue* q) {
    int item;
    if (is_empty(q)) {
        item = -1;
    }
    else {
        item = q->items[q->front];
        q->front++;
        if (q->front > q->rear) {
            q->front = q->rear = -1;
        }
    }
    return item;
}

// 
// END OF QUEUE FUNCTIONS
// 

// 
// GRAPH FUNCTIONS
// 

AdjVertexNode* new_vertex_node(int id)
{
    AdjVertexNode* new_node = (AdjVertexNode*) malloc(sizeof(AdjVertexNode));
    if (new_node == NULL) ERR("malloc");
    new_node->id = id;
    new_node->next = NULL;
    return new_node;
}

Graph* new_graph(int vertex_count)
{
    Graph* graph = (Graph*) malloc(sizeof(Graph));
    if (graph==NULL) ERR("malloc");

    graph->vertex_count = vertex_count;
    graph->vertices = (Vertex*) malloc(vertex_count * sizeof(Vertex));
    if (graph->vertices==NULL) ERR("malloc");

    for (int i = 0; i < vertex_count; i++) {
        graph->vertices[i].id = i;
        graph->vertices[i].head = NULL;
        graph->vertices[i].items[0] = (Item) { .id = -1, .dest_vertex_id = -1 };
        graph->vertices[i].items[1] = (Item) { .id = -1, .dest_vertex_id = -1 };
        graph->vertices[i].assigned_item_ids[0] = -1;
        graph->vertices[i].assigned_item_ids[1] = -1;
    }

    return graph;
}

int are_connected(Graph* graph, int i, int j) 
{
    AdjVertexNode* adj_vertex = graph->vertices[i].head;
    while (adj_vertex) {
        if (adj_vertex->id == j) {
            return 1;
        }
        adj_vertex = adj_vertex->next;
    }
    return 0;
}

void add_edge(Graph* graph, int i, int j)
{
    AdjVertexNode* new_node = new_vertex_node(j);
    new_node->next = graph->vertices[i].head;
    graph->vertices[i].head = new_node;

    new_node = new_vertex_node(i);
    new_node->next = graph->vertices[j].head;
    graph->vertices[j].head = new_node;
}

void safe_add_edge(Graph* graph, int i, int j)
{
    if (!are_connected(graph, i, j)) {
        add_edge(graph, i, j);
    }
}

int adjacent_count(Vertex V) {
    int count = 0;
    AdjVertexNode* temp = V.head;
    while (temp) {
        count++;
        temp = temp->next;
    }
    return count;
}

int random_adjacent_id(Graph* graph, int room_id) {
    int adj_count = adjacent_count(graph->vertices[room_id]);
    AdjVertexNode* temp = graph->vertices[room_id].head;
    int iterations = rand() % adj_count;
    for (int i=0; i<iterations; i++) {
        if (temp->next) {
            temp = temp->next;
        }
    } 
    return temp->id;
}

void print_map_info(Graph* graph, int player_location)
{
    printf("\nMAP INFO\n");
    for (int n = 0; n < graph->vertex_count; n++)
    {
        AdjVertexNode* adj_vertex = graph->vertices[n].head;
        
        printf("\nRoom ID %d", n);
        if (player_location == n) printf(" -----> [YOU ARE HERE]");
        printf("\nCurrent items [%d (dest %d), %d (dest %d)]\n", 
            graph->vertices[n].items[0].id, graph->vertices[n].items[0].dest_vertex_id, 
            graph->vertices[n].items[1].id, graph->vertices[n].items[1].dest_vertex_id);
        printf("Assigned item ids: [%d, %d]\n", graph->vertices[n].assigned_item_ids[0], graph->vertices[n].assigned_item_ids[1]);
        printf("Adjacent rooms: ");
        while (adj_vertex)
        {
            printf("%d ", adj_vertex->id);
            adj_vertex = adj_vertex->next;
        }
        printf("\n");
    }
}

int BFS(Graph* graph, int starting_id) 
{
    int* visited = (int*)calloc(graph->vertex_count, sizeof(int));
    if (visited==NULL) ERR("calloc");

    Queue* q = create_queue();
    visited[starting_id] = 1;
    enqueue(q, starting_id);

    while (!is_empty(q)) {
        int vertex_id = dequeue(q);
        AdjVertexNode* curr = graph->vertices[vertex_id].head;
        while (curr) {
            int curr_id = curr->id;

            if (visited[curr_id] == 0) {
                visited[curr_id] = 1;
                enqueue(q, curr_id);
            }
            curr = curr->next;
        }
    }

    for (int i = 0; i < graph->vertex_count; i++) {
        if (visited[i] == 0) {
            return 0;
        }
    }
    return 1;
}

Graph* generate_random_graph(int vertex_count) {
    Graph* graph = new_graph(vertex_count);
    srand(time(NULL));
    while (!BFS(graph, 0)) {
        safe_add_edge(graph, rand() % vertex_count, rand() % vertex_count);
    }
    return graph;
}

int save_graph_to_file(Graph* graph, char* path) {
    int fd;
    if ((fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR ))< 0) ERR("open");

    char buffer[1024*1024] = "";

    string_to_buffer(buffer, "VERT");
    int_to_buffer(buffer, graph->vertex_count);
    endline_to_buffer(buffer);

    for (int i=0; i<graph->vertex_count; i++) {
        string_to_buffer(buffer, "ID: ");
        int_to_buffer(buffer, i);

        string_to_buffer(buffer, "ADJ:");
        int adj = adjacent_count(graph->vertices[i]);
        int_to_buffer(buffer, adj);

        endline_to_buffer(buffer);

        AdjVertexNode* curr = graph->vertices[i].head;
        for (int j=1; j<=adj; j++) {
            int_to_buffer(buffer, curr->id);
            curr = curr->next;
            if (j == adj) {
                endline_to_buffer(buffer);
            }
        }
    }

    printf("%s", buffer);
    if ((write(fd, buffer, strlen(buffer)))<0) ERR("write");
    if (close(fd)) ERR("close");
    return (EXIT_SUCCESS);
}

Graph* read_graph_from_path(char* path) {

    int fd;
    if ((fd = open(path, O_RDONLY))<0) ERR("open");

    char buf[4];
    char endl[1];

    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, endl, 1))<0) ERR("read");

    int vertex_count = atoi(buf);
    Graph* graph = new_graph(vertex_count);

    for (int i=0; i<vertex_count; i++) {
        if ((read(fd, buf, 4))<0) ERR("read");
        if ((read(fd, buf, 4))<0) ERR("read");
        if ((read(fd, buf, 4))<0) ERR("read");
        if ((read(fd, buf, 4))<0) ERR("read");
        if ((read(fd, endl, 1))<0) ERR("read");

        int adjacent_count = atoi(buf);

        for (int j=0; j<adjacent_count; j++) {
            
            if ((read(fd, buf, 4))<0) ERR("read");
            int vertex_id = atoi(buf);
            safe_add_edge(graph, i, vertex_id);

            if (j == adjacent_count-1) if ((read(fd, endl, 1))<0) ERR("read");
        }
    }
    if (close(fd)) ERR("close");
    return graph;
}

int nftw_dir_count = 0;
int dirfinder_current_id;

int find_nftw_dir_count(const char *name, const struct stat* s, int type, struct FTW *f) {
    if (type == FTW_D) {
        nftw_dir_count++;
    }
    return 0;
}

void dirfinder(char* dir_path, Graph* graph, int parent_id) {

    DIR *dirp;
    struct dirent *dp;
    struct stat filestat;

    if (parent_id != dirfinder_current_id) {
        safe_add_edge(graph, dirfinder_current_id, parent_id);
        parent_id = dirfinder_current_id;
    }

    char* path = realpath(dir_path, NULL);
    if (chdir(path)) ERR("chdir");

    if (NULL == (dirp = opendir("."))) ERR("opendir");
    printf("ROOM %d = %s\n", dirfinder_current_id, path);

    errno = 0;
    do {
        if ((dp = readdir(dirp)) != NULL) {
            if (lstat(dp->d_name, &filestat)) ERR("lstat");
            if (S_ISDIR(filestat.st_mode)) {
                if (strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name, ".") == 0) continue;
                dirfinder_current_id++;
                dirfinder(dp->d_name, graph, parent_id);
                if (chdir(path)) ERR("chdir");
            }
        }
    } while (dp != NULL);

    if (errno != 0) ERR("readdir");
    if (closedir(dirp)) ERR("closedir");
}

void map_from_dir_tree(char* dir_path, char* file_path) {

    char* cwd = realpath(".", NULL);
    nftw(dir_path, find_nftw_dir_count, MAX_FD, FTW_PHYS);
    printf("\n[*] %d directories found in %s\n", nftw_dir_count, dir_path);
    
    if (nftw_dir_count < MIN_VERTEX_COUNT || nftw_dir_count > MAX_VERTEX_COUNT) {
        printf("\n[!] Please choose another directory such that:\nn - total number of directories and subdirectories\nn > %d && n < %d\n", MIN_VERTEX_COUNT, MAX_VERTEX_COUNT);
    } else {
        Graph* graph = new_graph(nftw_dir_count);
        dirfinder_current_id = 0;
        dirfinder(dir_path, graph, 0);
        print_map_info(graph, 0);

        if (chdir(cwd)) ERR("chdir");
        printf("\n[*] Saving map to %s ...\n", file_path);
        int err = save_graph_to_file(graph, file_path);
        if (!err) printf("[*] Map saved\n");
        else printf("\n[!] Error while saving the map.");
    }
}

// 
// END OF GRAPH FUNCTIONS
// 

// 
// PLAYER FUNCTIONS
// 

void print_player_info(Player* player) {
    printf("PLAYER INFO\n");
    printf("\nCurrent position: %d\n", player->location);
    printf("Current items [%d (dest %d), %d (dest %d)]\n", 
            player->items[0].id, player->items[0].dest_vertex_id, 
            player->items[1].id, player->items[1].dest_vertex_id);
}

void player_move(Game* game, int vertex_id) {
    int curr = game->player->location;
    if (are_connected(game->map, curr, vertex_id)) {
        printf("\n[*] Moved to %d.\n", vertex_id);
        game->player->location = vertex_id;
    } else {
        printf("\n[!] Error. Rooms %d and %d are not connected.\n", curr, vertex_id);
    }
}

// 
// END OF PLAYER FUNCTIONS
// 

// 
// START OF ITEM FUNCTIONS
// 

int items_assigned_count(Graph* graph, int vertex_id) {
    int items = 0;
    if (graph->vertices[vertex_id].assigned_item_ids[0] != -1) items++;
    if (graph->vertices[vertex_id].assigned_item_ids[1] != -1) items++;
    return items;
}

int items_currently_count(Graph* graph, int vertex_id) {
    int items = 0;
    if (graph->vertices[vertex_id].items[0].id != -1) items++;
    if (graph->vertices[vertex_id].items[1].id != -1) items++;
    return items;
}

int items_in_inventory(Player* player) {
    int items = 0;
    if (player->items[0].id != -1) items++;
    if (player->items[1].id != -1) items++;
    return items;
}

int total_item_count(Graph* graph, Player* player) {
    int count = 0;
    for (int i=0; i<graph->vertex_count; i++) {
        count += items_currently_count(graph, i);
    }
    count += items_in_inventory(player);
    return count;
}

void spawn_items(Game* game) {
    int item_count = floor(game->map->vertex_count*3/2);

    for (int item_id=0; item_id<item_count; item_id++) {

        int assigned_vertex_id = rand() % game->map->vertex_count;
        while (items_assigned_count(game->map, assigned_vertex_id) == 2) 
            assigned_vertex_id = rand() % game->map->vertex_count;
        
        game->map->vertices[assigned_vertex_id]
        .assigned_item_ids[items_assigned_count(game->map, assigned_vertex_id)] = item_id;

        int current_vertex_id = rand() % game->map->vertex_count;
        while (items_currently_count(game->map, current_vertex_id) == 2 || current_vertex_id == assigned_vertex_id)
            current_vertex_id = rand() % game->map->vertex_count;

        int item_idx = items_currently_count(game->map, current_vertex_id);
        game->map->vertices[current_vertex_id].items[item_idx].id = item_id;
        game->map->vertices[current_vertex_id].items[item_idx].dest_vertex_id = assigned_vertex_id;
    }
}

void pickup_item(Game* game, int item_id) {
    int room_id = game->player->location;
    if (game->map->vertices[room_id].items[0].id == item_id || game->map->vertices[room_id].items[1].id == item_id) {
        if (items_in_inventory(game->player) < 2) {
            int inventory_idx = items_in_inventory(game->player);
            game->player->items[inventory_idx].id = item_id;

            if (game->map->vertices[room_id].items[0].id == item_id) {
                game->player->items[inventory_idx].dest_vertex_id = game->map->vertices[room_id].items[0].dest_vertex_id;
                
                game->map->vertices[room_id].items[0].id = game->map->vertices[room_id].items[1].id;
                game->map->vertices[room_id].items[0].dest_vertex_id = game->map->vertices[room_id].items[1].dest_vertex_id;
                game->map->vertices[room_id].items[1].id = -1;
                game->map->vertices[room_id].items[1].dest_vertex_id = -1;
            } else {
                game->player->items[inventory_idx].dest_vertex_id = game->map->vertices[room_id].items[1].dest_vertex_id;
                
                game->map->vertices[room_id].items[1].id = -1;
                game->map->vertices[room_id].items[1].dest_vertex_id = -1;
            }
        } else {
            printf("\n[!] Error. Player's inventory is full.\n");
        }
    } else {
        printf("\n[!] Error. Item not found in Room %d.\n", room_id);
    }
}

void drop_item(Game* game, int item_id) {
    int room_id = game->player->location;
    if (game->player->items[0].id == item_id || game->player->items[1].id == item_id) {
        if (items_currently_count(game->map, room_id) < 2) {
            int inventory_idx = items_currently_count(game->map, room_id);
            game->map->vertices[room_id].items[inventory_idx].id = item_id;

            if (game->player->items[0].id == item_id) {
                game->map->vertices[room_id].items[inventory_idx].dest_vertex_id = game->player->items[0].dest_vertex_id;
                
                game->player->items[0].id = game->player->items[1].id;
                game->player->items[0].dest_vertex_id = game->player->items[1].dest_vertex_id;
                game->player->items[1].id = -1;
                game->player->items[1].dest_vertex_id = -1;
            } else {
                game->map->vertices[room_id].items[inventory_idx].dest_vertex_id = game->player->items[1].dest_vertex_id;
                
                game->player->items[1].id = -1;
                game->player->items[1].dest_vertex_id = -1;
            }
        } else {
            printf("\n[!] Error. Room is full.\n");
        }
    } else {
        printf("\n[!] Error. Item not found in player's inventory.\n");
    }
}

void swap_random_items(Game* game) {
    srand(time(NULL));
    int first_room = rand() % game->map->vertex_count;
    int second_room = rand() % game->map->vertex_count;

    while (items_currently_count(game->map, first_room) == 0)
        first_room = rand() % game->map->vertex_count;
    while (items_currently_count(game->map, second_room) == 0 || first_room == second_room)
        second_room = rand() % game->map->vertex_count;

    int idx_1 = rand() % items_currently_count(game->map, first_room);
    int idx_2 = rand() % items_currently_count(game->map, second_room);

    int temp_id = game->map->vertices[first_room].items[idx_1].id;
    int temp_dest = game->map->vertices[first_room].items[idx_1].dest_vertex_id;

    game->map->vertices[first_room].items[idx_1].id = game->map->vertices[second_room].items[idx_2].id;
    game->map->vertices[first_room].items[idx_1].dest_vertex_id = game->map->vertices[second_room].items[idx_2].dest_vertex_id;
    game->map->vertices[second_room].items[idx_2].id = temp_id;
    game->map->vertices[second_room].items[idx_2].dest_vertex_id = temp_dest;

    fprintf(stderr, "\n[*] Swapped item %d (dest %d) from Room ID %d with item %d (dest %d) from Room ID %d.\n",
        game->map->vertices[first_room].items[idx_1].id, game->map->vertices[first_room].items[idx_1].dest_vertex_id, second_room,
        game->map->vertices[second_room].items[idx_2].id, game->map->vertices[second_room].items[idx_2].dest_vertex_id, first_room);
}

// 
// END OF ITEM FUNCTIONS
//

// 
// GAME FUNCTIONS
// 

Game* new_game(Graph* map) {
    srand(time(NULL));

    Game* game = (Game*) malloc(sizeof(Game));
    if (game==NULL) ERR("malloc");

    game->map = map;
    game->player = (Player*) malloc(sizeof(Player));
    if (game->player==NULL) ERR("malloc");

    game->player->location = rand() % map->vertex_count;
    game->player->items[0] = (Item) { .id = -1, .dest_vertex_id = -1 };
    game->player->items[1] = (Item) { .id = -1, .dest_vertex_id = -1 };

    spawn_items(game);
    return game;
}

void print_game_state(Game* game) {
    printf("\n-------- GAME STATE --------\n\n");
    print_player_info(game->player);
    print_map_info(game->map, game->player->location);
    printf("\nITEMS IN TOTAL: %d [SHOULD BE %d]\n", total_item_count(game->map, game->player), (int) floor(game->map->vertex_count*3/2));
}

int save_game(Game* game, char* path) {
    int fd;
    if ((fd = open(path, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR ))<0) ERR("open");

    char buffer[1024*1024] = "";
    
    string_to_buffer(buffer, "PLYR");
    endline_to_buffer(buffer);

    string_to_buffer(buffer, "POS:");
    int_to_buffer(buffer, game->player->location);

    string_to_buffer(buffer, "ITM:");
    int_to_buffer(buffer, game->player->items[0].id);
    int_to_buffer(buffer, game->player->items[0].dest_vertex_id);
    int_to_buffer(buffer, game->player->items[1].id);
    int_to_buffer(buffer, game->player->items[1].dest_vertex_id);
    endline_to_buffer(buffer);

    string_to_buffer(buffer, "VERT");
    int_to_buffer(buffer, game->map->vertex_count);
    endline_to_buffer(buffer);

    for (int i=0; i<game->map->vertex_count; i++) {
        string_to_buffer(buffer, "ID: ");
        int_to_buffer(buffer, i);

        string_to_buffer(buffer, "ITM:");
        int_to_buffer(buffer, game->map->vertices[i].items[0].id);
        int_to_buffer(buffer, game->map->vertices[i].items[0].dest_vertex_id);
        int_to_buffer(buffer, game->map->vertices[i].items[1].id);
        int_to_buffer(buffer, game->map->vertices[i].items[1].dest_vertex_id);

        string_to_buffer(buffer, "ASG:");
        int_to_buffer(buffer, game->map->vertices[i].assigned_item_ids[0]);
        int_to_buffer(buffer, game->map->vertices[i].assigned_item_ids[1]);

        string_to_buffer(buffer, "ADJ:");
        int adj = adjacent_count(game->map->vertices[i]);
        int_to_buffer(buffer, adj);
        endline_to_buffer(buffer);

        AdjVertexNode* curr = game->map->vertices[i].head;
        for (int j=1; j<=adj; j++) {
            int_to_buffer(buffer, curr->id);
            curr = curr->next;
            if (j == adj) endline_to_buffer(buffer);
        }
    }
    if ((write(fd, buffer, strlen(buffer)))<0) ERR("write");
    if (close(fd)) ERR("close");
    return (EXIT_SUCCESS);
}

Game* load_game(char* path) {
    Game* game = (Game*) malloc(sizeof(Game));
    if (game==NULL) ERR("malloc");

    game->player = (Player*) malloc(sizeof(Player));
    if (game->player==NULL) ERR("malloc");

    int fd;
    if ((fd = open(path, O_RDONLY))<0) ERR("open");

    char buf[4] = "";
    char endl[1] = "";

    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, endl, 1))<0) ERR("read");
    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, buf, 4))<0) ERR("read");
    int location = atoi(buf);

    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, buf, 4))<0) ERR("read");
    int player_item_1 = atoi(buf);

    if ((read(fd, buf, 4))<0) ERR("read");
    int player_item_1_dest = atoi(buf);

    if ((read(fd, buf, 4))<0) ERR("read");
    int player_item_2 = atoi(buf);

    if ((read(fd, buf, 4))<0) ERR("read");
    int player_item_2_dest = atoi(buf);

    if ((read(fd, endl, 1))<0) ERR("read");

    game->player->location = location;
    game->player->items[0].id = player_item_1;
    game->player->items[0].dest_vertex_id = player_item_1_dest;
    game->player->items[1].id = player_item_2;
    game->player->items[1].dest_vertex_id = player_item_2_dest;

    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, buf, 4))<0) ERR("read");
    if ((read(fd, endl, 1))<0) ERR("read");
    int entries = atoi(buf);

    Graph* map = new_graph(entries);

    for (int i=0; i<entries; i++) {
        if ((read(fd, buf, 4))<0) ERR("read");
        if ((read(fd, buf, 4))<0) ERR("read");
        if ((read(fd, buf, 4))<0) ERR("read");

        if ((read(fd, buf, 4))<0) ERR("read");
        int item_1 = atoi(buf);

        if ((read(fd, buf, 4))<0) ERR("read");
        int item_1_dest = atoi(buf);

        if ((read(fd, buf, 4))<0) ERR("read");
        int item_2 = atoi(buf);

        if ((read(fd, buf, 4))<0) ERR("read");
        int item_2_dest = atoi(buf);

        map->vertices[i].items[0].id = item_1;
        map->vertices[i].items[0].dest_vertex_id = item_1_dest;
        map->vertices[i].items[1].id = item_2;
        map->vertices[i].items[1].dest_vertex_id = item_2_dest;

        if ((read(fd, buf, 4))<0) ERR("read");

        if ((read(fd, buf, 4))<0) ERR("read");
        int assigned_item_1 = atoi(buf);

        if ((read(fd, buf, 4))<0) ERR("read");
        int assigned_item_2 = atoi(buf);

        map->vertices[i].assigned_item_ids[0] = assigned_item_1;
        map->vertices[i].assigned_item_ids[1] = assigned_item_2;

        if ((read(fd, buf, 4))<0) ERR("read");

        if ((read(fd, buf, 4))<0) ERR("read");
        int adjacent_count = atoi(buf);

        if ((read(fd, endl, 1))<0) ERR("read");

        for (int j=0; j<adjacent_count; j++) {
            
            if ((read(fd, buf, 4))<0) ERR("read");
            int vertex_id = atoi(buf);
            safe_add_edge(map, i, vertex_id);

            if (j == adjacent_count-1) if ((read(fd, endl, 1))<0) ERR("read");
        }
    }

    if (close(fd)) ERR("close");
    game->map = map;
    return game;
}

// 
// END OF GAME FUNCTIONS
// 

// 
// THREAD FUNCTIONS
// 

void on_autosave_end() {
    fprintf(stderr, "[*] Autosave thread ended successfully!\n");
}

void autosave(thread_autosave* data) {
    struct timespec t = {1, 0};
    struct timespec current;
    fprintf(stderr, "[*] Autosave is enabled!\n");
    pthread_cleanup_push((void *) on_autosave_end, NULL);
    while(1) {
        nanosleep(&t, NULL);
        clock_gettime(CLOCK_REALTIME, &current);
        if ((ELAPSED(data->game_state->last_saved, current)) > 60) {
            fprintf(stderr, "\n[*] Autosaving to %s ...\n", data->path);
            pthread_mutex_lock(data->pmxGameState);
            int err = save_game(data->game_state, data->path);
            pthread_mutex_unlock(data->pmxGameState);
            if (!err) fprintf(stderr, "[*] Autosaved!\n");
            else fprintf(stderr, "[!] Errow while autosaving\n");
            clock_gettime(CLOCK_REALTIME, &data->game_state->last_saved);
        }
    }
    pthread_cleanup_pop(1);
}

int msp_length(msp_node* head) {
    int count = 0;
    msp_node* curr = head;
    while (curr->next) {
        count++;
        curr = curr->next;
    }
    return count;
}

void print_msp(msp_node* head) {
    msp_node* curr = head;
    printf("\nMODERATELY SHORT PATH:\n");
    printf("Current Room->");
    while(curr->next) {
        printf("%d", curr->room_id);
        curr = curr->next;
        if (curr->next) {
            printf("->");
        }
    }
    printf("\n");
}

void* find_path(void* voidPtr) {
    thread_pathfinder* data = voidPtr;

    srand(data->thread_id);

    msp_node* result;
    if (NULL==(result = (msp_node*) malloc(sizeof(msp_node)))) ERR("malloc");
    result->room_id = data->game_state->player->location; 
    result->next = NULL;

    int current_room_id = result->room_id;
    msp_node* temp = result;

    for (int i=0; i<1000; i++) {
        if (current_room_id == data->room_id) {
            break;
        }
        if (NULL==(temp->next = (msp_node*) malloc(sizeof(msp_node)))) ERR("malloc");
        temp->room_id = random_adjacent_id(data->game_state->map, current_room_id);
        current_room_id = temp->room_id;
        temp = temp->next;
    }

    return result;
}

void find_moderately_short_path(Game* game, int threads_count, int room_id) {

    thread_pathfinder* datas = (thread_pathfinder*) malloc(threads_count * sizeof(thread_pathfinder));
    if (datas==NULL) ERR("malloc");
    msp_node* subresult;
    msp_node* best_result;

    srand(time(NULL));
    for (int i=0; i<threads_count; i++) {
        datas[i].game_state = game;
        datas[i].room_id = room_id;
        int err = pthread_create(&datas[i].thread_id, NULL, find_path, &datas[i]);
        if (err != 0) ERR("pthread_create");
    }
    
    for (int i=0; i<threads_count; i++) {
        int err = pthread_join(datas[i].thread_id, (void*) &subresult);
        if (err != 0) ERR("pthread_join");
        if (NULL != subresult) {
            int new_path_length = msp_length(subresult);
            if (best_result) {
                int best_path_length = msp_length(best_result);
                if (new_path_length < best_path_length) {
                    free(best_result);
                    best_result = subresult;
                } else {
                    free(subresult);
                }
            } else {
                best_result = subresult;
            }
        }
    }
    if (msp_length(best_result)>999) {
        printf("\n[!] Error. None of the threads reached room %d.\n", room_id);
    } else {
        print_msp(best_result);
    }
    free(best_result);
    free(datas);
}

void on_sighandler_end() {
    fprintf(stderr, "[*] Signal handling thread ended successfully!\n");
}

void sigusr1_handler(thread_signal* data) {
    sigset_t new_mask;
    sigemptyset(&new_mask);
    sigaddset(&new_mask, SIGUSR1);

    fprintf(stderr, "[*] Signal handling is enabled!\n");
    pthread_cleanup_push((void *) on_sighandler_end, NULL);
    int sig;
    for (;;) {
        if(sigwait(&new_mask, &sig)) ERR("sigwait");
        switch (sig) {
            case SIGUSR1:
                fprintf(stderr,"\n[*] Signal handler catched SIGUSR1!\n");
                fprintf(stderr,"[*] Swapping two random items...\n");
                pthread_mutex_lock(data->pmxGameState);
                swap_random_items(data->game_state);
                pthread_mutex_unlock(data->pmxGameState);
                
                break;
            default:
                printf("[!] Unexpected signal!\n");
                exit(EXIT_FAILURE);
        }
    }
    pthread_cleanup_pop(1);
}

// 
// END OF THREADS FUNCTIONS 
// 

// 
// FLOW FUNCTIONS
// 

void usage(char *name){
    fprintf(stderr,"[!] USAGE: %s -b <backup-path>\n",name);
    exit(EXIT_FAILURE);
}

char* get_backup_path(int argc, char** argv) {
    if (argc == 3) {
        if (strcmp(argv[1], "-b") == 0) {
            return argv[2];
        } else {
            usage(argv[0]);
        }
    }
    char* env = getenv("GAME_AUTOSAVE");
    if (env) return env;
    return ".game-autosave";
} 

void show_main_menu() {
    printf("\nMAIN MENU:\n");
    printf("# read-map <map-path>\n");
    printf("# map-from-dir-tree <dir-path> <out-path>\n");
    printf("# generate-random-map <number-of-rooms> <out-path>\n");
    printf("# load-game <save-path>\n");
    printf("# exit\n");
}

void show_game_menu() {
    printf("\nGAME MENU:\n");
    printf("# move-to <room>\n");
    printf("# pick-up <item>\n");
    printf("# drop <item>\n");
    printf("# save <save-path>\n");
    printf("# find-path <number-of-threads> <room>\n");
    printf("# sigusr1\n");
    printf("# quit\n");
}

void start_game(Game* game, char* backup_path) {
    char user[MAX_INPUT_LENGTH];
    char arg[MAX_INPUT_LENGTH];

    print_game_state(game);
    show_game_menu();

    clock_gettime(CLOCK_REALTIME, &game->last_saved);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    pthread_mutex_t mxGameState = PTHREAD_MUTEX_INITIALIZER;
    thread_autosave data;
    data.path = backup_path;
    data.game_state = game;
    data.pmxGameState = &mxGameState;
    pthread_create(&data.thread_id, NULL, (void *) autosave, &data);

    thread_signal sig_data;
    sig_data.game_state = game;
    sig_data.pmxGameState = &mxGameState;
    pthread_create(&sig_data.thread_id, NULL, (void *) sigusr1_handler, &sig_data);

    while(1) {
        scanf("%s", user);
        pthread_mutex_lock(&mxGameState);
        if (strcmp(user, "move-to") == 0) {
            scanf("%s", arg);
            int vertex_id = atoi(arg);  
            player_move(game, vertex_id);
        }

        if (strcmp(user, "pick-up") == 0) {
            scanf("%s", arg);
            int item_id = atoi(arg);  
            pickup_item(game, item_id);
        }

        if (strcmp(user, "drop") == 0) {
            scanf("%s", arg);
            int item_id = atoi(arg);  
            drop_item(game, item_id);
        }

        if (strcmp(user, "save") == 0) {
            scanf("%s", arg);  
            int err = save_game(game, arg);
            if (!err) printf("\n[*] Game saved to %s!\n", arg);
            else printf("\n[!] Error while saving the game.\n");
            clock_gettime(CLOCK_REALTIME, &game->last_saved);
        }
        pthread_mutex_unlock(&mxGameState);

        if (strcmp(user, "find-path") == 0) {
            scanf("%s", arg);
            int threads_count = atoi(arg);
            scanf("%s", arg);
            int room_id = atoi(arg);
            if (threads_count > MAX_PATHFINDING_THREADS || threads_count < 1) {
                printf("\n[!] Please, let the computer breathe, choose number of threads <= %d\n", MAX_PATHFINDING_THREADS);
            } else {
                find_moderately_short_path(game, threads_count, room_id);
            }
        }

        if (strcmp(user, "sigusr1") == 0) {
            struct timespec t = {1, 0};
            kill(0, SIGUSR1);
            nanosleep(&t, NULL);
        }

        if (strcmp(user, "quit") == 0) {
            pthread_cancel(data.thread_id);
            pthread_cancel(sig_data.thread_id);
            break;
        }
        
        print_game_state(game);
        show_game_menu();
    }
}

int main(int argc, char** argv) {
    show_main_menu();

    char* backup_path = get_backup_path(argc, argv);

    char user[MAX_INPUT_LENGTH];
    char file_path[MAX_INPUT_LENGTH];

    while(1) {
        scanf("%s", user);

        if (strcmp(user, "read-map") == 0) {  
            scanf("%s", file_path);
            Graph* graph = read_graph_from_path(file_path);
            Game* game = new_game(graph);
            start_game(game, backup_path);
        }
        else if (strcmp(user, "generate-random-map") == 0) {
            int n;
            scanf("%d", &n);
            scanf("%s", file_path);
            if (n < MIN_VERTEX_COUNT) {
                printf("\n[!] Please, at least %d vertices...\n", MIN_VERTEX_COUNT);
                continue;
            }
            if (n > MAX_VERTEX_COUNT) {
                printf("\n[!] Huh, let your computer breathe, choose n <= %d please!\n", MAX_VERTEX_COUNT);
                continue;
            }
            Graph* graph = generate_random_graph(n);
            if (save_graph_to_file(graph, file_path) == 0) {
                printf("\n[*] Successfully saved map (%s).\n", file_path);
            }
        }
        else if (strcmp(user, "map-from-dir-tree") == 0) {
            char dir_path[MAX_INPUT_LENGTH]; 
            scanf("%s", dir_path);
            scanf("%s", file_path);
            map_from_dir_tree(dir_path, file_path);  
        }
        else if (strcmp(user, "load-game") == 0) {  
            scanf("%s", file_path);
            Game* game = load_game(file_path);
            start_game(game, backup_path);
        }
        else if (strcmp(user, "exit") == 0) {  
            break;
        }
        else {
            show_main_menu();
        }
    }
    exit(EXIT_SUCCESS); 
}

// 
// END OF FLOW FUNCTIONS
// 
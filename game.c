/*
 * game.c
 * GBA game program
 */

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

/* include the background image we are using */
#include "GBAProjectBackground1.h"
#include "background.h"

/* include the sprite image we are using */
#include "Sprites.h"

/* include the tile map we are using */
#include "ForestBackground.h"

/* the tile mode flags needed for display control register */
#define MODE0 0x00
#define BG0_ENABLE 0x100

/* flags to set sprite handling in display control register */
#define SPRITE_MAP_2D 0x0
#define SPRITE_MAP_1D 0x40
#define SPRITE_ENABLE 0x1000


/* the control registers for the four tile layers */
volatile unsigned short* bg0_control = (volatile unsigned short*) 0x4000008;

/* palette is always 256 colors */
#define PALETTE_SIZE 256

/* there are 128 sprites on the GBA */
#define NUM_SPRITES 128

/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

/* the memory location which controls sprite attributes */
volatile unsigned short* sprite_attribute_memory = (volatile unsigned short*) 0x7000000;

/* the memory location which stores sprite image data */
volatile unsigned short* sprite_image_memory = (volatile unsigned short*) 0x6010000;

/* the address of the color palettes used for backgrounds and sprites */
volatile unsigned short* bg_palette = (volatile unsigned short*) 0x5000000;
volatile unsigned short* sprite_palette = (volatile unsigned short*) 0x5000200;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

/* scrolling registers for backgrounds */
volatile short* bg0_x_scroll = (unsigned short*) 0x4000010;
volatile short* bg0_y_scroll = (unsigned short*) 0x4000012;

/* the bit positions indicate each button - the first bit is for A, second for
 * B, and so on, each constant below can be ANDED into the register to get the
 * status of any one button */
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

/* the scanline counter is a memory cell which is updated to indicate how
 * much of the screen has been drawn */
volatile unsigned short* scanline_counter = (volatile unsigned short*) 0x4000006;

/* wait for the screen to be fully drawn so we can do something during vblank */
void wait_vblank() {
    /* wait until all 160 lines have been updated */
    while (*scanline_counter < 160) { }
}

/* this function checks whether a particular button has been pressed */
unsigned char button_pressed(unsigned short button) {
    /* and the button register with the button constant we want */
    unsigned short pressed = *buttons & button;

    /* if this value is zero, then it's not pressed */
    if (pressed == 0) {
        return 1;
    } else {
        return 0;
    }
}

/* return a pointer to one of the 4 character blocks (0-3) */
volatile unsigned short* char_block(unsigned long block) {
    /* they are each 16K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x4000));
}

/* return a pointer to one of the 32 screen blocks (0-31) */
volatile unsigned short* screen_block(unsigned long block) {
    /* they are each 2K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x800));
}

/* flag for turning on DMA */
#define DMA_ENABLE 0x80000000

/* flags for the sizes to transfer, 16 or 32 bits */
#define DMA_16 0x00000000
#define DMA_32 0x04000000

/* pointer to the DMA source location */
volatile unsigned int* dma_source = (volatile unsigned int*) 0x40000D4;

/* pointer to the DMA destination location */
volatile unsigned int* dma_destination = (volatile unsigned int*) 0x40000D8;

/* pointer to the DMA count/control */
volatile unsigned int* dma_count = (volatile unsigned int*) 0x40000DC;

/* copy data using DMA */
void memcpy16_dma(unsigned short* dest, unsigned short* source, int amount) {
    *dma_source = (unsigned int) source;
    *dma_destination = (unsigned int) dest;
    *dma_count = amount | DMA_16 | DMA_ENABLE;
}

/* function to setup background 0 for this program */
void setup_background() {

    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) bg_palette, (unsigned short*) GBAProjectBackground1_palette, PALETTE_SIZE);

    /* load the image into char block 0 */
    memcpy16_dma((unsigned short*) char_block(0), (unsigned short*) GBAProjectBackground1_data,
            (GBAProjectBackground1_width * GBAProjectBackground1_height) / 2);
            

    /* set all control the bits in this register */
    *bg0_control = 0 |    /* priority, 0 is highest, 3 is lowest */
        (0 << 2)  |       /* the char block the image data is stored in */
        (0 << 6)  |       /* the mosaic flag */
        (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
        (16 << 8) |       /* the screen block the tile data is stored in */
        (0 << 13) |       /* wrapping flag */
        (0 << 14);        /* bg size, 0 is 256x256 */
        

    /* load the tile data into screen block 16 */
    memcpy16_dma((unsigned short*) screen_block(16), (unsigned short*) ForestBackground, ForestBackground_width * ForestBackground_height);
    
}


/* just kill time */
void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

/* a sprite is a moveable image on the screen */
struct Sprite {
    unsigned short attribute0;
    unsigned short attribute1;
    unsigned short attribute2;
    unsigned short attribute3;
};

/* array of all the sprites available on the GBA */
struct Sprite sprites[NUM_SPRITES];
int next_sprite_index = 0;

/* the different sizes of sprites which are possible */
enum SpriteSize {
    SIZE_8_8,
    SIZE_16_16,
    SIZE_32_32,
    SIZE_64_64,
    SIZE_16_8,
    SIZE_32_8,
    SIZE_32_16,
    SIZE_64_32,
    SIZE_8_16,
    SIZE_8_32,
    SIZE_16_32,
    SIZE_32_64
};

/* function to initialize a sprite with its properties, and return a pointer */
struct Sprite* sprite_init(int x, int y, enum SpriteSize size,
        int horizontal_flip, int vertical_flip, int tile_index, int priority) {

    /* grab the next index */
    int index = next_sprite_index++;

    /* setup the bits used for each shape/size possible */
    int size_bits, shape_bits;
    switch (size) {
        case SIZE_8_8:   size_bits = 0; shape_bits = 0; break;
        case SIZE_16_16: size_bits = 1; shape_bits = 0; break;
        case SIZE_32_32: size_bits = 2; shape_bits = 0; break;
        case SIZE_64_64: size_bits = 3; shape_bits = 0; break;
        case SIZE_16_8:  size_bits = 0; shape_bits = 1; break;
        case SIZE_32_8:  size_bits = 1; shape_bits = 1; break;
        case SIZE_32_16: size_bits = 2; shape_bits = 1; break;
        case SIZE_64_32: size_bits = 3; shape_bits = 1; break;
        case SIZE_8_16:  size_bits = 0; shape_bits = 2; break;
        case SIZE_8_32:  size_bits = 1; shape_bits = 2; break;
        case SIZE_16_32: size_bits = 2; shape_bits = 2; break;
        case SIZE_32_64: size_bits = 3; shape_bits = 2; break;
    }

    int h = horizontal_flip ? 1 : 0;
    int v = vertical_flip ? 1 : 0;

    /* set up the first attribute */
    sprites[index].attribute0 = y |             /* y coordinate */
        (0 << 8) |          /* rendering mode */
        (0 << 10) |         /* gfx mode */
        (0 << 12) |         /* mosaic */
        (1 << 13) |         /* color mode, 0:16, 1:256 */
        (shape_bits << 14); /* shape */

    /* set up the second attribute */
    sprites[index].attribute1 = x |             /* x coordinate */
        (0 << 9) |          /* affine flag */
        (h << 12) |         /* horizontal flip flag */
        (v << 13) |         /* vertical flip flag */
        (size_bits << 14);  /* size */

    /* setup the second attribute */
    sprites[index].attribute2 = tile_index |   // tile index */
        (priority << 10) | // priority */
        (0 << 12);         // palette bank (only 16 color)*/

    /* return pointer to this sprite */
    return &sprites[index];
}

/* update all of the spries on the screen */
void sprite_update_all() {
    /* copy them all over */
    memcpy16_dma((unsigned short*) sprite_attribute_memory, (unsigned short*) sprites, NUM_SPRITES * 4);
}

/* setup all sprites */
void sprite_clear() {
    /* clear the index counter */
    next_sprite_index = 0;

    /* move all sprites offscreen to hide them */
    for(int i = 0; i < NUM_SPRITES; i++) {
        sprites[i].attribute0 = SCREEN_HEIGHT;
        sprites[i].attribute1 = SCREEN_WIDTH;
    }
}

/* set a sprite postion */
void sprite_position(struct Sprite* sprite, int x, int y) {
    /* clear out the y coordinate */
    sprite->attribute0 &= 0xff00;

    /* set the new y coordinate */
    sprite->attribute0 |= (y & 0xff);

    /* clear out the x coordinate */
    sprite->attribute1 &= 0xfe00;

    /* set the new x coordinate */
    sprite->attribute1 |= (x & 0x1ff);
}

/* move a sprite in a direction */
void sprite_move(struct Sprite* sprite, int dx, int dy) {
    /* get the current y coordinate */
    int y = sprite->attribute0 & 0xff;

    /* get the current x coordinate */
    int x = sprite->attribute1 & 0x1ff;

    /* move to the new location */
    sprite_position(sprite, x + dx, y + dy);
}

/* change the vertical flip flag */
void sprite_set_vertical_flip(struct Sprite* sprite, int vertical_flip) {
    if (vertical_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x2000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xdfff;
    }
}

/* change the horizontal flip flag */
void sprite_set_horizontal_flip(struct Sprite* sprite, int horizontal_flip) {
    if (horizontal_flip) {
        /* set the bit */
        sprite->attribute1 |= 0x1000;
    } else {
        /* clear the bit */
        sprite->attribute1 &= 0xefff;
    }
}

/* change the tile offset of a sprite */
void sprite_set_offset(struct Sprite* sprite, int offset) {
    /* clear the old offset */
    sprite->attribute2 &= 0xfc00;

    /* apply the new one */
    sprite->attribute2 |= (offset & 0x03ff);
}

/* setup the sprite image and palette */
void setup_sprite_image() {
    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) sprite_palette, (unsigned short*) Sprites_palette, PALETTE_SIZE);

    /* load the image into sprite image memory */
    memcpy16_dma((unsigned short*) sprite_image_memory, (unsigned short*) Sprites_data, (Sprites_width * Sprites_height) / 2);
}

/* a struct for the koopa's logic and behavior */
struct Player {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y postion in pixels */
    int x, y;

    /* the koopa's y velocity in 1/256 pixels/second */
    //int yvel;

    /* the koopa's y acceleration in 1/256 pixels/second^2 */
    //int gravity; 

    /* which frame of the animation he is on */
    int frame;

    /* the number of frames to wait before flipping */
    int animation_delay;
    
    int animation_state;

    /* the animation counter counts how many frames until we flip */
    int counter;

    /* whether the koopa is moving right now or not */
    int move;
    
    int facing;

    /* the number of pixels away from the edge of the screen the koopa stays */
    int border;

    /* if the koopa is currently falling */
    int health;
    
    int invincible;
};

/* initialize the koopa */
void player_init(struct Player* player) {
    player->x = 100;
    player->y = 113;
    player->border = 40;
    player->frame = 0;
    player->move = 0;
    player->counter = 0;
    player->animation_delay = 8;
    player->animation_state = 0;
    player->facing = 0;
    player->health = 3;
    player->invincible = 0;
    player->sprite = sprite_init(player->x, player->y, SIZE_16_16, 0, 0, player->frame, 1);
}

struct Bullet {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y postion in pixels */
    int x, y;
    
    int dx, dy;
    
    int transparent;
};

void bullet_init(struct Bullet* bullet) {
    bullet->x = 0;
    bullet->y = 0;
    bullet->dx = 0;
    bullet->dy = 0;
    bullet->transparent = 1;
    bullet->sprite = sprite_init(bullet->x, bullet->y, SIZE_8_8, 0, 0, 90, 1);
}

struct Slime{
     /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y postion in pixels */
    int x, y;
    
    int health;
    
    int frame;
    
    int animation_delay;
    
    int animation_state;
    
    int wait;
    
    int dead;
    
    int delay;
    
    int id;
};

void slime_init(struct Slime* slime, int id, int delay){
    slime->x = 240;
    slime->y = 240;
    slime->health = 1;
    slime->frame = 64;
    slime->animation_delay = 8;
    slime->animation_state = 0;
    slime->wait = 6;
    slime->dead = 0;
    slime->delay = delay;
    slime->id = id;
    slime->sprite = sprite_init(slime->x, slime->y, SIZE_16_16, 0, 0, slime->frame, 2);
}
    
    

/* finds which tile a screen coordinate maps to, taking scroll into acco  unt */
unsigned short tile_lookup(int x, int y, int xscroll, int yscroll,
        const unsigned short* tilemap, int tilemap_w, int tilemap_h) {

    /* adjust for the scroll */
    x += xscroll;
    y += yscroll;

    /* convert from screen coordinates to tile coordinates */
    x >>= 3;
    y >>= 3;

    /* account for wraparound */
    while (x >= tilemap_w) {
        x -= tilemap_w;
    }
    while (y >= tilemap_h) {
        y -= tilemap_h;
    }
    while (x < 0) {
        x += tilemap_w;
    }
    while (y < 0) {
        y += tilemap_h;
    }

    /* the larger screen maps (bigger than 32x32) are made of multiple stitched
       together - the offset is used for finding which screen block we are in
       for these cases */
    int offset = 0;

    /* if the width is 64, add 0x400 offset to get to tile maps on right   */
    if (tilemap_w == 64 && x >= 32) {
        x -= 32;
        offset += 0x400;
    }

    /* if height is 64 and were down there */
    if (tilemap_h == 64 && y >= 32) {
        y -= 32;

        /* if width is also 64 add 0x800, else just 0x400 */
        if (tilemap_w == 64) {
            offset += 0x800;
        } else {
            offset += 0x400;
        }
    }

    /* find the index in this tile map */
    int index = y * 32 + x;

    /* return the tile */
    return tilemap[index + offset];
}

/* move the koopa left or right returns if it is at edge of the screen */
int player_left(struct Player* player, int xscroll, int yscroll) {
    /* face left */
    sprite_set_horizontal_flip(player->sprite, 1);
    player->move = 1;
    player->facing = 1;
    unsigned short tile = tile_lookup(player->x, player->y+1, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
        return 0; 
    }
    
    unsigned short tile2 = tile_lookup(player->x, player->y+15, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
        return 0; 
    }
    
    /* if we are at the left end, just scroll the screen */
    if (player->x < player->border) {
        return 1;
    } else {
        /* else move left */
        player->x--;
        return 0;
    }
}

int player_right(struct Player* player, int xscroll, int yscroll) {
    /* face right */
    sprite_set_horizontal_flip(player->sprite, 0);
    player->move = 1;
    player->facing = 2;
    
    unsigned short tile = tile_lookup(player->x+16, player->y+1, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
        return 0; 
    }
    
    unsigned short tile2 = tile_lookup(player->x+16, player->y+15, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
        return 0; 
    }

    /* if we are at the right end, just scroll the screen */
    if (player->x > (SCREEN_WIDTH - 16 - player->border)) {
        return 1;
    } else {
        /* else move right */
        player->x++;
        return 0;
    }
}

int player_up(struct Player* player, int xscroll, int yscroll) {
    /* face right */
    sprite_set_horizontal_flip(player->sprite, 0);
    player->move = 2;
    player->facing = 3;
    
    unsigned short tile = tile_lookup(player->x+1, player->y, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
        return 0; 
    }
    
    unsigned short tile2 = tile_lookup(player->x+15, player->y, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
        return 0; 
    }

    /* if we are at the right end, just scroll the screen */
    if (player->y < player->border) {
        return 1;
    } else {
        /* else move right */
        player->y--;
        return 0;
    }
}

int player_down(struct Player* player, int xscroll, int yscroll) {
    /* face right */
    sprite_set_horizontal_flip(player->sprite, 0);
    player->move = 3;
    player->facing = 0;
    
    unsigned short tile = tile_lookup(player->x+1, player->y+16, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
        return 0; 
    }
    
    unsigned short tile2 = tile_lookup(player->x+15, player->y+16, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
        return 0; 
    }

    /* if we are at the right end, just scroll the screen */
    if (player->y > (SCREEN_HEIGHT - 16 - player->border)) {
        return 1;
    } else {
        /* else move right */
        player->y++;
        return 0;
    }
}

/* stop the player from walking */
void player_stop(struct Player* player) {
    player->move = 0;
    if (player->facing == 0){
    	player->frame = 0;
    }
    if (player->facing == 1 || player->facing == 2){
    	player->frame = 24;
    }
    if (player->facing == 3){
    	player->frame = 40;
    }	
    player->counter = 7;
    sprite_set_offset(player->sprite, player->frame);
}


void shoot(struct Player* player, struct Bullet* bullet){
    bullet->x = player->x+8;
    bullet->y = player->y+8;
    if (player->facing == 0) {
    	bullet->dy = 1;
    }
    
    if (player->facing == 1) {
    	bullet->dx = -1;
    }
    
    if (player->facing == 2) {
    	bullet->dx = 1;
    }
    
    if (player->facing == 3) {
    	bullet->dy = -1;
    }	
    sprite_set_offset(bullet->sprite, 88);
    bullet->transparent = 0;
}

void slime_move(struct Slime* slime, struct Player* player, int xscroll, int yscroll, int wave){
    if (slime->wait > 0){
    	slime->wait--;
    	return;
    }

    if (player->x > slime->x){
    	if (player->y > slime->y && player->y - slime->y > player->x - slime->x){
    	    unsigned short tile = tile_lookup(slime->x+1, slime->y+16, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
    	    unsigned short tile2 = tile_lookup(player->x+15, player->y+16, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->y++;
    	} else if (player->y < slime->y && slime->y - player->y > player->x - slime->x){
    	    unsigned short tile = tile_lookup(slime->x+1, slime->y, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
    	    unsigned short tile2 = tile_lookup(player->x+15, player->y, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->y--;
    	} else {
    	
    	    unsigned short tile = tile_lookup(slime->x+16, slime->y+1, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
	    unsigned short tile2 = tile_lookup(player->x+16, player->y+15, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->x++;
    	}
    } else if (player->x < slime->x){
        if (player->y > slime->y && player->y - slime->y > slime->x - player->x){
    	    unsigned short tile = tile_lookup(slime->x+1, slime->y+16, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
    	    unsigned short tile2 = tile_lookup(player->x+15, player->y+16, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->y++;
    	} else if (player->y < slime->y && slime->y - player->y > slime->x - player->x){
    	    unsigned short tile = tile_lookup(slime->x+1, slime->y, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
    	    unsigned short tile2 = tile_lookup(player->x+15, player->y, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->y--;
    	} else {
    	
    	    unsigned short tile = tile_lookup(slime->x, slime->y+1, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
	    unsigned short tile2 = tile_lookup(player->x, player->y+15, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->x--;
    	}
    	
    } else {
        if (player->y < slime->y){
            unsigned short tile = tile_lookup(slime->x+1, slime->y, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
    	    unsigned short tile2 = tile_lookup(player->x+15, player->y, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->y--;
    	} else {
    	    unsigned short tile = tile_lookup(slime->x+1, slime->y+16, xscroll, yscroll, ForestBackground,
            ForestBackground_width, ForestBackground_height);
    
    	    if (tile == 1 || tile == 2 || tile == 5 || tile == 6){
            	return; 
    	    }
    
    	    unsigned short tile2 = tile_lookup(player->x+15, player->y+16, xscroll, yscroll, ForestBackground,
            	ForestBackground_width, ForestBackground_height);
    
    	    if (tile2 == 1 || tile2 == 2 || tile2 == 5 || tile2 == 6){
            	return; 
    	    }
    	    slime->y++;
    	}
    	
    }
    slime->wait = 6-wave;
}
    	
/*check if bullet hits anything */
int bullet_check(struct Bullet* bullet, struct Slime* slime) {
    if (bullet->x+4 > slime->x && bullet->x+4 < slime->x+16 && bullet->y+4 > slime->y && bullet->y+4 < slime->y+16 && slime->dead == 0) {
    	bullet->x = 0;
	bullet->y = 0;
	bullet->dx = 0;
	bullet->dy = 0;
	bullet->transparent = 1;
	sprite_set_offset(bullet->sprite, 90);
	slime->x = 240;
	slime->y = 240;
	slime->dead=1;    	
    }
}

/* update the player */
void player_update(struct Player* player, int xscroll) {


    /* update animation if moving */
    if (player->move == 1) {
        player->counter++;
        if (player->counter >= player->animation_delay) {
            if (player->animation_state == 0) {
                player->frame = 24;
                player->animation_state = 1;
            } else {
                player->frame = 32;
                player->animation_state = 0;
            }
            sprite_set_offset(player->sprite, player->frame);
            player->counter = 0;
        }
    }
    
    if (player->move == 2) {
        player->counter++;
        if (player->counter >= player->animation_delay) {
            if (player->animation_state == 0) {
                player->frame = 48;
                player->animation_state = 1;
            } else {
                player->frame = 56;
                player->animation_state = 0;
            }
            sprite_set_offset(player->sprite, player->frame);
            player->counter = 0;
        }
    }
    
    if (player->move == 3) {
        player->counter++;
        if (player->counter >= player->animation_delay) {
            if (player->animation_state == 0) {
                player->frame = 8;
                player->animation_state = 1;
            } else {
                player->frame = 16;
                player->animation_state = 0;
            }
            sprite_set_offset(player->sprite, player->frame);
            player->counter = 0;
        }
    }

    /* set on screen position */
    sprite_position(player->sprite, player->x, player->y);
}

void update_bullet(struct Bullet* bullet){
    if (bullet->transparent == 0){
    	bullet->x = bullet->x + bullet->dx;
    	bullet->y = bullet->y + bullet->dy;
    	if (bullet->x > SCREEN_WIDTH || bullet->y > SCREEN_HEIGHT || bullet->x < 0 || bullet->y < 0){
	    bullet->x = 0;
	    bullet->y = 0;
	    bullet->dx = 0;
	    bullet->dy = 0;
	    bullet->transparent = 1;
	    sprite_set_offset(bullet->sprite, 90);
    	}
    	sprite_position(bullet->sprite, bullet->x, bullet->y);
    }
}

void update_slime(struct Slime* slime){
    if (slime->dead == 1){
	slime->delay=500;
	slime->dead=0;
    }
    if (slime->delay == 0){
    	if (slime->id == 1){  	
            sprite_position(slime->sprite, 120, 0);
            slime->x = 120;
            slime->y = 0;
        }
        if (slime->id == 2){
            sprite_position(slime->sprite, 120, 144);
            slime->x = 120;
            slime->y = 144;
        }
        if (slime->id == 3){
            sprite_position(slime->sprite, 16, 80);
            slime->x = 16;
            slime->y = 80;
        }
        if (slime->id == 4){
            sprite_position(slime->sprite, 224, 80);
            slime->x = 224;
            slime->y = 80;
        }
        slime->delay = -1;
    } else if(slime->delay<0){
   	sprite_position(slime->sprite, slime->x, slime->y);    
    } else {
    	sprite_position(slime->sprite, 240, 240);
    	slime->delay= slime->delay-1;
    }   	
}

void collision_check(struct Player* player, struct Slime* slime){
    if (player->x >= slime->x && player->x < slime->x+16 && player->y >= slime->y && player->y < slime->y+16 || player->x+16 >= slime->x && player->x+16 < slime->x+16 && player->y >= slime->y && player->y < slime->y+16 || player->x >= slime->x && player->x < slime->x+16 && player->y+16 >= slime->y && player->y+16 < slime->y+16 || player->x+16 >= slime->x && player->x+16 < slime->x+16 && player->y+16 >= slime->y && player->y+16 < slime->y+16){
    	if (player->invincible == 0){
    	    player->health == player->health-1;
    	    player->invincible == 30;
    	}
    }	
}

int calc_wave(int kills, int wave);

/* the main function */
int main() {
    /* we set the mode to mode 0 with bg0 on */
    *display_control = MODE0 | BG0_ENABLE | SPRITE_ENABLE | SPRITE_MAP_1D;

    /* setup the background 0 */
    setup_background();

    /* setup the sprite image data */
    setup_sprite_image();

    /* clear all the sprites on screen now */
    sprite_clear();

    /* create the koopa */
    struct Player player;
    player_init(&player);
    
    struct Bullet bullet1;
    bullet_init(&bullet1);
    
    struct Bullet bullet2;
    bullet_init(&bullet2);
    
    struct Bullet bullet3;
    bullet_init(&bullet3);
    
    struct Slime slime1;
    slime_init(&slime1, 1, 100);
    
    struct Slime slime2;
    slime_init(&slime2, 2, 400);
    
    struct Slime slime3;
    slime_init(&slime3, 3, 800);
    
    struct Slime slime4;
    slime_init(&slime4, 4, 1000);
    
    int bullet_delay = 0;

    /* set initial scroll to 0 */
    int xscroll = 0;
    int yscroll = 0;
    
    int kills = 0;
    int wave = 0; 

    /* loop forever */
    while (1) {
        /* update sprites */
        player_update(&player, xscroll);
        update_bullet(&bullet1);
        update_bullet(&bullet2);
        update_bullet(&bullet3);
        update_slime(&slime1);  
        update_slime(&slime2);
        update_slime(&slime3);
        update_slime(&slime4);  
   
               

        /* now the arrow keys move the koopa */
        if (button_pressed(BUTTON_RIGHT)) {
            if (player_right(&player, xscroll, yscroll)) {
                xscroll++;
                slime1.x--;
                slime2.x--;
                slime3.x--;
                slime4.x--;                
            }
        } else if (button_pressed(BUTTON_LEFT)) {
            if (player_left(&player, xscroll, yscroll)) {
                xscroll--;
                slime1.x++;
                slime2.x++;
                slime3.x++;
                slime4.x++;
            }
        } else if (button_pressed(BUTTON_UP)) {
            if (player_up(&player, xscroll, yscroll)) {
                yscroll--;
                slime1.y++;
                slime2.y++;
                slime3.y++;
                slime4.y++;
            }
        } else if (button_pressed(BUTTON_DOWN)) {
            if (player_down(&player, xscroll, yscroll)) {
                yscroll++;
                slime1.y--;
                slime2.y--;
                slime3.y--;
                slime4.y--;
            }
        } else {
            player_stop(&player);
        }

        /* check for jumping */
        if (button_pressed(BUTTON_A) && bullet_delay == 0) { 
            if (bullet1.transparent == 1){
            	shoot(&player, &bullet1);
            } else if (bullet2.transparent == 1){
            	shoot(&player, &bullet2);
            } else if (bullet3.transparent == 1){
            	shoot(&player, &bullet3);
            }
            bullet_delay = 20;            	
        }
        
        if (slime1.dead==0 && slime1.delay < 0){
            slime_move(&slime1, &player, xscroll, yscroll, wave);
        }
        if (slime2.dead==0 && slime2.delay < 0){
            slime_move(&slime2, &player, xscroll, yscroll, wave);
        }
        if (slime3.dead==0 && slime3.delay < 0){
            slime_move(&slime3, &player, xscroll, yscroll, wave);
        }
        if (slime4.dead==0 && slime4.delay < 0){
            slime_move(&slime4, &player, xscroll, yscroll, wave);
        }
        
        bullet_check(&bullet1, &slime1);
        bullet_check(&bullet2, &slime1);
        bullet_check(&bullet3, &slime1);
        
        bullet_check(&bullet1, &slime2);
        bullet_check(&bullet2, &slime2);
        bullet_check(&bullet3, &slime2);
        
        bullet_check(&bullet1, &slime3);
        bullet_check(&bullet2, &slime3);
        bullet_check(&bullet3, &slime3);
        
        bullet_check(&bullet1, &slime4);
        bullet_check(&bullet2, &slime4);
        bullet_check(&bullet3, &slime4);
        
        if (slime1.dead=1){
            kills++;
        }
        if (slime2.dead=1){
            kills++;
        }
        if (slime3.dead=1){
            kills++;
        }
        if (slime4.dead=1){
            kills++;
        }
                
        if (bullet_delay != 0){
            bullet_delay = bullet_delay-1;
        }
        
        wave = calc_wave(kills, wave);
        collision_check(&player, &slime1);
        collision_check(&player, &slime2);
        collision_check(&player, &slime3);
        collision_check(&player, &slime4);
        player.invincible = player.invincible-1;
        
        if (player.health==0){
	    sprite_clear();
	    player_init(&player);
	    bullet_init(&bullet1);
	    bullet_init(&bullet2);
	    bullet_init(&bullet3);
	    slime_init(&slime1, 1, 100);
	    slime_init(&slime2, 1, 400);
	    slime_init(&slime3, 1, 800);
	    slime_init(&slime4, 1, 1000);
	    kills = 0;
	    wave=0;
	}
        
        /* wait for vblank before scrolling and moving sprites */
        wait_vblank();
        *bg0_x_scroll = xscroll;
        *bg0_y_scroll = yscroll;
        sprite_update_all();

        /* delay some */
        delay(300);
    }
}


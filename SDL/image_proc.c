#include "image_proc.h"

/*
    Please remove or modify the gate in line 50
    if screenshot capabilities for windows/mac are added

*/


SDL_Surface *surface = NULL;
char save_bmp_filename[50] = {'\0'};
char rom_title[17] = {'\0'};

//hold the path to the rom
char *rom_path = NULL;

//final path and name of screenshot, extra large just in case
char final_image_filename[2048] = {'\0'};


//final filename must be at least 49 characters long
//(17 for GB_get_rom_title, 32 for 32digit generated randnum)
void generate_image_filename(GB_gameboy_t *gb){

    char rand_to_digit[33] = {'\0'};

    uint32_t random_num = rand();


    GB_get_rom_title(gb, rom_title);

    if(rom_title == NULL){

        fprintf(stderr, "%s", "Getting title failed");
    }

    if(!sprintf(rand_to_digit, "%u", random_num)){

        fprintf(stderr, "%s", "Failed to translate random num to string.");

    }

    strcat(save_bmp_filename, rom_title);
    strcat(save_bmp_filename, rand_to_digit);
}

void generate_BMPimage(GB_gameboy_t *gb, void *current_pixel_buffer, void *secondary_pixel_buffer){

        #ifndef __linux__
            return; // gate non-linux
        #endif // __linux__


        uint32_t tempbuff[224 * 256];
        void *save_buffer = NULL;

        uint32_t rmask, gmask, bmask, amask;


        //swap out the active buffer


        save_buffer = current_pixel_buffer;
        current_pixel_buffer = tempbuff;


        if(SDL_BYTEORDER == SDL_LIL_ENDIAN){

            rmask = 0x000000FF;
            gmask = 0x0000FF00;
            bmask = 0x00FF0000;
            amask = 0xFF000000;

        }
        else{

            rmask = 0xFF000000;
            gmask = 0x00FF0000;
            bmask = 0x0000FF00;
            amask = 0x000000FF;

        }


        surface = SDL_CreateRGBSurfaceFrom(save_buffer, GB_get_screen_width(gb), GB_get_screen_height(gb),
                                            32, GB_get_screen_width(gb) * sizeof(uint32_t),
                                            rmask, gmask, bmask, amask);


        if(surface == NULL){

            fprintf(stderr, "%s", SDL_GetError());

        }

        //swap the saved buffer into correct position
        if(current_pixel_buffer == tempbuff){
            current_pixel_buffer = save_buffer;
        }
        else{

            secondary_pixel_buffer = save_buffer;

        }


        //get title of rom for screenshot and screenshot directory
        GB_get_rom_title(gb, rom_title);

        create_path_to_screenshot_directory(gb);

        generate_image_filename(gb);

        strcat(final_image_filename, save_bmp_filename);

        int print_success = SDL_SaveBMP(surface, final_image_filename);

        memset(save_bmp_filename, '\0', 50); //stuff gets whack if you take too many

        if(print_success != 0){
            fprintf(stderr, "%s", SDL_GetError());

        }


        SDL_FreeSurface(surface); //isekai surface kun


 }

int create_path_to_screenshot_directory(GB_gameboy_t *gb){ //save screeshots to directory in the directory of the rom

    int create_screenshot_dir_success = 0;

    //get path to current rom
    int pathlength = strlen(rom_path);
    int index = 0;

    //reverse through path to first '/'
    for(int i = pathlength; i >= 0; i--){

        if(rom_path[i] == '/'){

            index = i;
            break;
        }

    }

    //create temp filepath and copy
    char temp_path[1023] = {0}; //cant be 1kB because compiler compains about appending / in stdcat below


    for(int i = 0; i < index; i++){
        temp_path[i] = rom_path[i];
    }

    strcat(temp_path, "/");
    strcat(temp_path, rom_title);


    //create screenshot directory for roms if it doesnt exist
    #if __linux__


        DIR* screenshot_dir = opendir(temp_path);
        if(screenshot_dir == NULL){

            closedir(screenshot_dir);
            create_screenshot_dir_success = mkdir(temp_path, 0766);

        }
        else{
            closedir(screenshot_dir);
        }


    #elif _WIN32

        //sorry windows

    #else
        return -1

    #endif

        sprintf(final_image_filename, "%s%s", temp_path,"/");


        return create_screenshot_dir_success;
}






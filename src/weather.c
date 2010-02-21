/****************************************************************************
 *             ___________.__               .__                             *
 *             \_   _____/|  | ___.__. _____|__|__ __  _____                *
 *              |    __)_ |  |<   |  |/  ___/  |  |  \/     \               *
 *              |        \|  |_\___  |\___ \|  |  |  /  Y Y  \              *
 *             /_______  /|____/ ____/____  >__|____/|__|_|  /              *
 *                     \/      \/         \/     Engine    \/               *
 *                       A SMAUG Derived Game Engine.                       *
 * ------------------------------------------------------------------------ *
 * Elysium Engine Copyright 1999-2009 by Steven Loar                        *
 * Elysium Engine Development Team: Kayle (Steven Loar), Venia, Scoyn,      *
 *                                  and Mikon.                              *
 * ------------------------------------------------------------------------ *
 * [S]imulated [M]edieval [A]dventure multi[U]ser [G]ame                    *
 * SMAUG 1.4 (C) 1994, 1995, 1996, 1998  by Derek Snider                    *
 * SMAUG code team: Thoric, Altrag, Blodkai, Narn, Haus, Scryn, Rennard,    *
 * Swordbearer, Gorog, Grishnakh, Nivek, Tricops and Fireblade              *
 * ------------------------------------------------------------------------ *
 * Merc 2.1 Diku Mud improvments copyright (C) 1992, 1993 by Michael        *
 * Chastain, Michael Quan, and Mitchell Tse.                                *
 * Original Diku Mud copyright (C) 1990, 1991 by Sebastian Hammer,          *
 * Michael Seifert, Hans Henrik St{rfeldt, Tom Madsen, and Katja Nyboe.     *
 * ------------------------------------------------------------------------ *
 *	                     Weather System Header                             *
 ****************************************************************************
 *     Base Weather Model Copyright (c) 2007 Chris Jacobson                 *
 ****************************************************************************/

#include <string.h>
#include <stdio.h>
#include "mud.h"


const char *const hemisphere_name[] = {
   "northern", "southern"
};

const char *const climate_names[] = {
   "rainforest", "savanna", "desert", "steppe", "chapparal",
   "grasslands", "deciduous_forest", "taiga", "tundra", "alpine",
   "arctic" 
};

int get_hemisphere( char *type )
{
   unsigned int x;

   for( x = 0; x < ( sizeof( hemisphere_name ) / sizeof( hemisphere_name[0] ) ); x++ )
      if( !str_cmp( type, hemisphere_name[x] ) )
         return x;
   return -1;
}

int get_climate( char *type )
{
   unsigned int x;

   for( x = 0; x < ( sizeof( climate_names ) / sizeof( climate_names[0] ) ); x++ )
      if( !str_cmp( type, climate_names[x] ) )
         return x;
   return -1;
}

struct WeatherCell
{
	int climate;        // Climate flag for the cell
	int hemisphere;     // Hemisphere flag for the cell
	int	temperature;    // Fahrenheit because I'm American, by god
	int	pressure;       // 0..100 for now, later change to barometric pressures
	int	cloudcover;     // 0..100, amount of clouds in the sky
	int	humidity;       // 0+
	int	precipitation;  // 0..100
	int energy;			// 0..100 Storm Energy, chance of storm.
    /*
     *  Instead of a wind direction we use an X/Y speed
     *  It makes the math below much simpler this way.
     *  Its not hard to determine a basic cardinal direction from this
     *  If you want to, a good rule of thumb is that if one directional
     *  speed is more than double that of the other, ignore it; that is
     *  if you have speed X = 15 and speed Y = 3, the wind is obviously
     *  to the east.  If X = 15 and Y = 10, then its a south-east wind. 
     */
	int	windSpeedX;    //  < 0 = west, > 0 = east
	int	windSpeedY;    //  < 0 = north, > 0 = south
};

/*
  *	This is the Weather Map.  It is a grid of cells representing X-mile square
  *	areas of weather
  */
struct WeatherCell	weatherMap[WEATHER_SIZE_X][WEATHER_SIZE_Y];

 /*
  *	This is the Weather Delta Map.  It is used to accumulate changes to be
  *	applied to the Weather Map.  Why accumulate changes then apply them, rather
  *	than just change the Weather Map as we go?
  *	Because doing that can mean a change just made to a neighbor can
  *	immediately cause ANOTHER change to a neighbor, causing things
  *	to get out of control or causing cascading weather, propagating much
  *	faster and unpredictably (in a BAD unpredictable way)
  *	Instead, we determine all the changes that should occur based on the current
  *	'snapshot' of weather, than apply them all at once!
  */
struct WeatherCell	weatherDelta[WEATHER_SIZE_X][WEATHER_SIZE_Y];

//	Set everything up with random non-equal values to prevent equalibrium
void InitializeWeatherMap( void )
{
  int x, y;
  
  
	for( y = 0; y < WEATHER_SIZE_Y; y++ )
	{
		for( x = 0; x < WEATHER_SIZE_X; x++ )
		{
			struct WeatherCell *cell = &weatherMap[x][y];

			cell->climate		= number_range( 0, 10 );
			cell->hemisphere	= number_range( 0, 1 );
			cell->temperature	= number_range( -30, 100 );	
			cell->pressure		= number_range( 0, 100 );	
			cell->cloudcover    = number_range( 0, 100 );	
			cell->humidity		= number_range( 0, 100 );	
			cell->precipitation	= number_range( 0, 100 );	
			cell->windSpeedX	= number_range( -100, 100 );	
			cell->windSpeedY	= number_range( -100, 100 );
			cell->energy		= number_range( 0, 100 );
		}
	}
}

//Used to determine whether a field exceeds a certain number, see Weather messages for examples
bool ExceedsThreshold( int initial, int delta, int threshold ) 
{
    return ( ( initial < threshold ) && ( initial + delta >= threshold ) );
}

//Used to determin whether a field drops below a certain point, see Weather messages for examples.
bool DropsBelowThreshold( int initial, int delta, int threshold )
{ 
    return ( ( initial >= threshold ) && ( initial + delta < threshold ) ); 
}

//Send a message to a player in the area, assuming they are outside, and awake.
void WeatherMessage( const char *txt, int x, int y )
{
   AREA_DATA *pArea;
   DESCRIPTOR_DATA *d = NULL;

   for( pArea = first_area; pArea; pArea = pArea->next )
   {
      if( pArea->weatherx == x && pArea->weathery == y )
      {
           for (std::list<DESCRIPTOR_DATA * >::iterator iter = descriptor_list.begin(); 
                iter != descriptor_list.end(); 
                iter++ )
           {
              d = *iter;
            if( d->connected == CON_PLAYING )
            {
               if( d->character && ( d->character->in_room->area == pArea ) && IS_OUTSIDE( d->character )
                && !NO_WEATHER_SECT( d->character->in_room->sector_type ) && IS_AWAKE( d->character ) )
                  send_to_char( txt, d->character );
            }
         }
      }
   }
}

// This is where we apply all the functions and determine what message to send.
void ApplyDeltaChanges( void )
{
	int x, y; 

	for( y = 0; y < WEATHER_SIZE_Y; y++ )
	{
		for( x = 0; x < WEATHER_SIZE_X; x++ )
		{
			struct WeatherCell *cell = &weatherMap[x][y];
			struct WeatherCell *delta = &weatherDelta[x][y];

			if( isTorrentialDownpour( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain turns to snow as it continues to come down blizzard-like.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe blizzard turns to a cold rain as it continues to come in a torrential downpour.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 91 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain begins to increase in intensity falling heavily and quickly.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain begins to increase in intensity falling heavily and quickly.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 91 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain changes over to snow as the intensity increases, making a blinding white wall.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 91 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe heavy snow increases and freezes creating a blizzard.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 91 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe snow changes over to rain as it pounds down heavier.&D\r\n&YThunder and lightning begin to shake the gound and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe snow changes over to rain as it pounds down heavier.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WThe snow falls down fast and steady creating a blizzard.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain continues to pound the earth in a downpour.&D\r\n&YThunder and lightning boom and cackle and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain continues to pound the earth in a downpour.&D\r\n", x, y );


			}
			else if( isRainingCatsAndDogs( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe heavy rain turns into a heavy snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe heavy snow turns into a heavy rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 90 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy, fierce rain eases a bit, but continues to drum down.&D\r\n&YThunder and lightling light up the sky and shake the earth.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy, fierce rain eases a bit, but still continues to drum down.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 90 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WAs the heavy, fierce rain lessens a bit, it changes over to a steady snow.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 90 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe snow lessens and changes to a heavy rain.&D\r\n&YThunder and lightning begin to shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe snow lessens and changes to a heavy rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 90 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe intense snow lessens a little.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 80 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain increases in intensity.&D\r\n&YThunder and lightning begin to shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain increases in intensity.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 80 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain changes over to snow and begins to come down even harder.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 80 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe snows falls harder creating a wall of white.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 80 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe snow increases in intensity a bit and changes over to a heavy rain.&D\r\n&YThunder and lightning begin to shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe snow increases in intensity a bit and changes over to a heavy rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WThe snow comes down heavy creating a wall of white.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain comes down in big, heavy droplets.&D\r\n&YThunder and lightning boom, cackle and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain comes down in big, heavy droplets.&D\r\n", x, y );
			}
			else if( isPouring( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe intense rain changes over to a heavy snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe heavy snow changes over to a pouring rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 80 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy rain lessens a little.&D\r\n&YThunder and lightning boom, cackle, and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy rain lessens but still continues to pour down.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 80 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain lessens and turns to a heavy snow.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 80 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe wall of snow lessens as it turns to a pouring rain.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe wall of snow lessens as it turns to a pouring rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 80 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe intense snowfall lessens and continues coming down heavily.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 70 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain begins to pound down hard into a pouring rain.&D\r\n&YLightning flashes in the sky, accompanied shortly by booming thunder.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe steady rain begins to come down harder.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 70 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe heavy rain increases in intensity and changes over to snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 70 ) && cell->temperature <= 32 )
					WeatherMessage( "&The snow begins to fall more heavily and coat the ground quickly.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 70 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy snow changes over to rain and begins to pour down.&D\r\n&YLightning and thunder begin to light up the sky and shake the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy snow changes over to rain and begins to pour down.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WThe snow comes down heavily.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain pours down on the ground.&D\r\n&YLightning and thunder light up the sky and shake the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain pours down on the ground.&D\r\n", x, y );
			}
			else if( isRaingingHeavily( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe heavy rain changes over to a heavy snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe steady snow changes over to a heavy rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 70 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe pouring rain lessens a little.&D\r\n&YLightning and thunder begin to light up the sky and shake the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe pouring rain lessens a little.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 70 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe pouring rain lessens but changes over to a steady snow.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 70 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy snow changes over to a heavy rain.&D\r\n&YThunder cracks, and lightning flashes in the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy snow lessens a bit and changes over to a steady rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 70 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe heavy snow lessens a bit to a steady snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 60 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe pouring rain increases in intensity.  Lightning and thunder begin to light up the sky and shake the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe pouring rain begins to come down harder pounding the ground.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 60 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe pouring rain begins to increase in intensity and change over to a heavy snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 60 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe heavy snow increases in intensity creating a blanket of white on the ground.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 60 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe steady snow changes over to an increasingly heavier rain.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe steady snow changes over to an increasingly heavier rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WSnow falls heavily down on the ground.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BRain falls heavily down on the ground.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BRain falls heavily down on the ground.&D\r\n", x, y );
			}
			else if( isDownpour( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe pouring rain changes over to a thick snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe steady snow changes over to rain and lessens a bit.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 60 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy rain lessens a bit.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy rain lessens a bit.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 60 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe heavy rain lessens a bit and changes over to snow covering the ground in a blanket of white.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 60 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy snow lessens a bit as it changes over to a steady rain.&D\r\n&YLightning and thunder cackle, rattle, and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy snow lessens a bit as it changes over to a steady rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 60 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe heavy snow eases a bit.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 50 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe heavy snow eases up a bit as it changes over to a steady rain.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe heavy snow eases up a bit as it changes over to a steady rain.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 50 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe steady rain picks up and changes over to a heavy, steady snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 50 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe steady snow picks up a bit creating a blanket of white on the ground.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 50 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe steady snow increases in intensity as it changes over to a pouring rain.&D\r\n&YLightning streaks accross the sky and thunder booms shaking the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe steady snow increases in intensity as it changes over to a pouring rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WSnow falls steadily down.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain pours down.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain pours down.&D\r\n", x, y );
			}
			else if( isRainingSteadily( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe steady rain changes over to a steady snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe steady snow changes over to a steady rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 50 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe intense rain eases a bit to a steady rain.&D\r\n&YLightning makes the sky glow while thunder booms constantly.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe intense rain eases a bit to a steady rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 50 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe pouring rain lessens a bit and changes over to a steady snow.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 50 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe blanket of snow lessens a bit as it changes over to a steady rain.&D\r\n&YLightning lights up the sky and thunder shakes the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe blanket of snow lessens a bit as it changes over to a steady rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 50 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe blanket of snow eases up a bit to a steady snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 40 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain picks up in intensity.&D\r\n&YThunder shakes the ground and lightning illuminates the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain picks up in intensity.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 40 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain picks up and changes over to a steady snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 40 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe snow picks up in intensity.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 40 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe snow picks up in intensity as it changes over to a steady rain.&D\r\n&YLightning and thunder illuminate the sky and make the ground shake.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe snow picks up a bit as it changes over to a steady rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WThe snow falls steadily.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe rain falls steadily.&D\r\n&YThunder and lightning shake the ground and light up the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe rain falls steadily.&D\r\n", x, y );
			}
			else if( isRaining( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain changes over to snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe snow changes over to rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 40 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe steady rain eases up a bit.&D\r\n&YTightning and thunder illuminate the sky and shake the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe steady rain eases up a bit.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 40 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe steady rain eases a bit as it changes over to snow.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 40 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe steady snow eases a bit as it changes over to rain.&D\r\n&YThunder and lightning begin to shake the ground and illuminate the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe steady snow eases up a bit as it changes over to rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 40 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe steady snow eases up a bit.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 30 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe light rain picks up a bit.&D\r\n&YLightning illuminates the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe light rain picks up a bit.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 30 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe light rain picks up a bit as it changes over to snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 30 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe light snow picks up a bit.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 30 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BThe light snow increases in intensity as it changes over to rain.&D\r\n&YThunder booms and shakes the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe light snow increases in intensity as it changes over to rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WSnow continues to fall from the sky.&D\r\n", x, y );
					else if( isStormy( getEnergy( cell ) ) )
						WeatherMessage( "&BRain continues to fall from the sky.&D\r\n&YThunder and lightning boom, cackle, and shake the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BRain continues to fall from the sky.&D\r\n", x, y );
			}
			else if( isRainingLightly( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe light rain changes over to a light snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe light snow changes over to a light rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 30 ) )
					WeatherMessage( "&BThe rain eases a bit creating a light rain falling.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 30 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe rain eases a bit and changes over to a light snow.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 30 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe snow eases a bit as it changes over to a light rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 30 ) && cell->temperature <= 32 )
					WeatherMessage( "The snow eases up a bit to a light snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 20 ) )
					WeatherMessage( "&BThe drizzle picks up a bit to a light rain.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 20 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe drizzle picks up a bit and changes over to a light snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 20 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe flurries pick up a bit to a light snow.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 20 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe flurries pick up a bit and change over to a light rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WThe light snow continues to fall gently on the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BThe light rain continues to fall gently on the ground.&D\r\n", x, y );
			}
			else if( isDrizzling( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "WThe drizzle changes over to flurries.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe flurries change over to a drizzling rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 20 ) )
					WeatherMessage( "&BThe light rain eases up a bit to just a drizzle.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 20 )
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe light rain eases up a bit and changes over to a few flurries.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 20 )
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe light snow eases up a bit and changes over to just a drizzle.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 20 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe light snow lessens to just a few flurries.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 10 ) )
					WeatherMessage( "&BThe mist picks up a bit to a drizzling rain.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 10 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe mist picks up a bit and changes over to some flurries.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 10 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe scattered flakes pick up a bit to some flurries.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->precipitation, delta->precipitation, 10 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe few scattered flakes pick up and change over to a drizzling rain.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WFlurries of snow fall to the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BA light drizzling rain falls to the ground.&D\r\n", x, y );
			}
			else if( isMisting( getPrecip( cell ) ) )
			{
				if( DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe mist of rain changes over to a few scattered snowflakes.&D\r\n", x, y );
				else if( ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe few scattered snowflakes change over to a misting rain.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 10 ) )
					WeatherMessage( "&BThe drizzle lessens down to a light mist.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 10 ) 
					&& DropsBelowThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&WThe drizzle lessens and changes over to just a few scattered snowflakes.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 10 ) && cell->temperature <= 32 )
					WeatherMessage( "&WThe flurries of snow lessen to just a few scattered snowflakes.&D\r\n", x, y );
				else if( DropsBelowThreshold( cell->precipitation, delta->precipitation, 10 ) 
					&& ExceedsThreshold( cell->temperature, delta->temperature, 32 ) )
					WeatherMessage( "&BThe flurries of snow lessen and change over to a light mist.&D\r\n", x, y );
				else
					if( cell->temperature <= 32 )
						WeatherMessage( "&WA few scattered snowflakes fall to the ground.&D\r\n", x, y );
					else
						WeatherMessage( "&BA light mist falls to the ground.&D\r\n", x, y );				
			}
			else
			{
				if( isExtremelyCloudy( getCloudCover( cell ) ) )
				{
					if( ExceedsThreshold( cell->cloudcover, delta->cloudcover, 80 ) )
						WeatherMessage( "&wMore clouds roll in creating a blanket over the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&wClouds cover the sky like a blanket.&D\r\n", x, y );
				}
				else if( isModeratelyCloudy( getCloudCover( cell ) ) )
				{
					if( ExceedsThreshold( cell->cloudcover, delta->cloudcover, 60 ) )
						WeatherMessage( "&wThe sky begins to get more cloudy.&D\r\n", x, y );
					else if( DropsBelowThreshold( cell->cloudcover, delta->cloudcover, 80 ) )
						WeatherMessage( "&wSome of the clouds begin to break.&D\r\n", x, y );
					else
						WeatherMessage( "&wMany clouds cover the sky.&D\r\n", x, y );
				}
				else if( isPartlyCloudy( getCloudCover( cell ) ) )
				{				
					if( ExceedsThreshold( cell->cloudcover, delta->cloudcover, 40 ) )
						WeatherMessage( "&wMore clouds roll in making it partly cloudy.&D\r\n", x, y );
					else if( DropsBelowThreshold( cell->cloudcover, delta->cloudcover, 60 ) )
						WeatherMessage( "&wSome of the clouds move out clearing part of the sky.&D\r\n", x, y );
					else
						WeatherMessage( "&wClouds cover part of the sky.&D\r\n", x, y );
				}
				else if( isCloudy( getCloudCover( cell ) ) )
				{
					if( ExceedsThreshold( cell->cloudcover, delta->cloudcover, 20 ) )
						WeatherMessage( "&wA few clouds begin to roll into the sky.&D\r\n", x, y );
					else if( DropsBelowThreshold( cell->cloudcover, delta->cloudcover, 40 ) )
						WeatherMessage( "&wA few of the clouds begin to move out leaving only a few clouds left behind.&D\r\n", x, y );
					else
						WeatherMessage( "&wA few clouds hover in the sky.&D\r\n", x, y );
				}
				else
				{
					if( DropsBelowThreshold( cell->cloudcover, delta->cloudcover, 20 ) )
						WeatherMessage( "&wThe few remaining clouds begin to roll out.&D\r\n", x, y );
					else
						if( isSwelteringHeat( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 90 ) )
								WeatherMessage( "&OIt begins to warm up making the already intense heat almost unbearable.&D\r\n", x, y );
							else
								WeatherMessage( "&OThe heat is almost unbearable.&D\r\n", x, y );
						}
						else if( isVeryHot( getTemp( cell ) ) )
						{ 
							if( ExceedsThreshold( cell->temperature, delta->temperature, 80 ) )
								WeatherMessage( "&OAs the temperature increases, the heat begins to become intense.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 90 ) )
								WeatherMessage( "&OThe unbearable heat eases a bit.&D\r\n", x, y );
							else
								WeatherMessage( "&OIt is very hot.&D\r\n", x, y );
						}
						else if( isHot( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 70 ) )
								WeatherMessage( "&OThe temperature rises making it quite hot.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 80 ) )
								WeatherMessage( "&OThe temperature lessens slightly, making it a little more bearable.&D\r\n", x, y );
							else
								WeatherMessage( "&OIt is hot.&D\r\n", x, y );
						}
						else if( isWarm( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 60 ) )
								WeatherMessage( "&OThe nice temperature heats up a little making it warm.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 70 ) )
								WeatherMessage( "&OThe heat becomes a little less intense making it warm.&D\r\n", x, y );
							else
								WeatherMessage( "&OIt is a little warm.&D\r\n", x, y );
						}
						else if( isTemperate( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 50 ) )
								WeatherMessage( "&OIt warms a little bit taking away the chill and making it nice and pleasant.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 60 ) )
								WeatherMessage( "&OThe heat eases and makes it nice and pleasant.&D\r\n", x, y );
							else
								WeatherMessage( "&OThe temperature is nice and pleasant.&D\r\n", x, y );
						}
						else if( isCool( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 40 ) )
								WeatherMessage( "&CThe chilly air warms up a bit leaving it cool.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 50 ) )
								WeatherMessage( "&CThe temperature drops leaving it a little cool.&D\r\n", x, y );
							else
								WeatherMessage( "&CThe temperature is cool.&D\r\n", x, y );
						}
						else if( isChilly( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 30 ) )
								WeatherMessage( "&CThe temperature rises a little bit but there is still a chill in the air.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 40 ) )
								WeatherMessage( "&CThe temperature drops as the air takes on a chilly feel.&D\r\n", x, y );
							else
								WeatherMessage( "&CIt is quite chilly.&D\r\n", x, y );
						}
						else if( isCold( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 20 ) )
								WeatherMessage( "&CThe frigid temperature warms up a tad making it chilly.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 30 ) )
								WeatherMessage( "&CThe temperature drops making it cold.&D\r\n", x, y );
							else
								WeatherMessage( "&CIt is cold.&D\r\n", x, y );
						}
						else if( isFrosty( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 10 ) )
								WeatherMessage( "&CThe freezing temperature warms up a bit leaving it frigid.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 20 ) )
								WeatherMessage( "&The cold temperature drops making it frigid.&D\r\n", x, y );
							else
								WeatherMessage( "&CThe temperate is very frigid.&D\r\n", x, y );
						}
						else if( isFreezing( getTemp( cell ) ) )
						{
							if( ExceedsThreshold( cell->temperature, delta->temperature, 0 ) )
								WeatherMessage( "&CThe freezing cold begins to warm up slightly.&D\r\n", x, y );
							else if( DropsBelowThreshold( cell->temperature, delta->temperature, 10 ) )
								WeatherMessage( "&CThe frigid temperature drops making it freezing.&D\r\n", x, y );
							else
								WeatherMessage( "&CIt is freezing cold.&D\r\n", x, y );
						}
						else if( isReallyCold( getTemp( cell ) ) )
						{
							if( DropsBelowThreshold( cell->temperature, delta->temperature, 0 ) )
								WeatherMessage( "&CThe temperature drops making the freezing cold worse.&D\r\n", x, y );
							else if( ExceedsThreshold( cell->temperature, delta->temperature, -10 ) )
								WeatherMessage( "&CThe temperature warms up the very cold air a little.&D\r\n", x, y );
							else
								WeatherMessage( "&CIt is really cold.&D\r\n", x, y );
						}
						else if( isVeryCold( getTemp( cell ) ) )
						{
							if( DropsBelowThreshold( cell->temperature, delta->temperature, -10 ) )
								WeatherMessage( "&CThe temperature drops making it all the more cold.&D\r\n", x, y );
							else if( ExceedsThreshold( cell->temperature, delta->temperature, -20 ) )
								WeatherMessage( "&CThe temperature rises making the unbearable cold a little better.&D\r\n", x, y );
							else
								WeatherMessage( "&CIt is very cold.&D\r\n", x, y );
						}
						else if( isExtremelyCold( getTemp( cell ) ) )
						{
							if( DropsBelowThreshold( cell->temperature, delta->temperature, -20 ) )
								WeatherMessage( "&CThe already very cold temperature drops making it unbearable.&D\r\n", x, y );
							else
								WeatherMessage( "&CIt is unbearablly cold.&D\r\n", x, y );
						}
					}
				}
            //Here we actually apply the changes making sure they stay within specific bounds
			cell->temperature	= WRANGE( -30, cell->temperature + delta->temperature, 100 );
			cell->pressure		= WRANGE( 0, cell->pressure + delta->pressure, 100 );
			cell->cloudcover    = WRANGE( 0, cell->cloudcover + delta->cloudcover, 100 );
			cell->energy		= WRANGE( 0, cell->energy + delta->energy, 100 );
			cell->humidity		= WRANGE( 0, cell->humidity + delta->humidity, 100 );
			cell->precipitation = WRANGE( 0, cell->precipitation + delta->precipitation, 100 );
			cell->windSpeedX	= WRANGE( -100, cell->windSpeedX + delta->windSpeedX, 100 );
			cell->windSpeedY	= WRANGE( -100, cell->windSpeedY + delta->windSpeedY, 100 );
		}
	}
}

void ClearWeatherDeltas( void )
{	//  Clear delta map
	memset( weatherDelta, 0, sizeof( weatherDelta ) );
}

void CalculateCellToCellChanges( void )
{
	int x, y;
	int rand;

	/*
     *  Randomly pick a cell to apply a random change to prevent equilibrium
     */
	x = number_range( 0, WEATHER_SIZE_X );
	y = number_range( 0, WEATHER_SIZE_Y );

	struct	WeatherCell *randcell = &weatherMap[x][y]; // Weather Cell
	rand = number_range( -10, 10 );

	switch( dice( 1, 8 ) )
	{
		case 1:
			randcell->cloudcover += rand;
			break;
		case 2:
			randcell->energy += rand;
			break;
		case 3:
			randcell->humidity += rand;
			break;
		case 4:
			randcell->precipitation += rand;
			break;
		case 5:
			randcell->pressure += rand;
			break;
		case 6:
			randcell->temperature += rand;
			break;
		case 7:
			randcell->windSpeedX += rand;
			break;
		case 8:
			randcell->windSpeedY += rand;
			break;
	}




   /*
    *  Iterate over every cell and set up the changes
    *  that will occur in that cell and it's neighbors
    *  based on the weather
    */
	for( y = 0; y < WEATHER_SIZE_Y; y++ )
	{
		for( x = 0; x < WEATHER_SIZE_X; x++ )
		{
		
		struct	WeatherCell *cell = &weatherMap[x][y];    //  Weather cell
		struct	WeatherCell *delta = &weatherDelta[x][y]; //  Where we accumulate the changes to apply

			/*
			 *  Here we force the system to take day/night into account
			 *  when determining temperature change.
			 */
			if( ( time_info.sunlight == SUN_RISE ) || ( time_info.sunlight == SUN_LIGHT ) )
				delta->temperature += ( number_range( -1, 2 ) + ( ( ( getCloudCover( cell ) / 10 ) > 5 ) ? -1 : 1 ) );
			if( ( time_info.sunlight == SUN_SET ) || ( time_info.sunlight == SUN_DARK ) )
				delta->temperature += ( number_range( -2, 0 ) + ( ( ( getCloudCover( cell ) / 10 ) < 5 ) ? 2 : -3 ) );

			//  Precipitation drops humidity by 5% of precip level
			if( cell->precipitation > 40 )
				delta->humidity -= ( cell->precipitation / 20 ); 
			else
				delta->humidity += number_range( 0, 3 );

			//  Humidity and pressure can affect the precipitation level
			int humidityAndPressure = ( cell->humidity + cell->pressure );
			if( ( humidityAndPressure / 2 ) >= 60 )
				delta->precipitation	+= ( cell->humidity / 10 );
			else if( ( humidityAndPressure / 2 ) < 60 && ( humidityAndPressure / 2 ) > 40 )
				delta->precipitation	+= number_range( -2, 2 );
			else if( ( humidityAndPressure / 2 ) <= 40 )
				delta->precipitation	-= ( cell->humidity / 5 );

			//  Humidity and precipitation can affect the cloud cover
			int humidityAndPrecip = ( cell->humidity + cell->precipitation );
			if( ( humidityAndPrecip / 2 ) >= 60 )
				delta->cloudcover	-= ( cell->humidity / 10 );
			else if( ( humidityAndPrecip / 2 ) < 60 && ( humidityAndPrecip / 2 ) > 40 )
				delta->cloudcover	+= number_range( -2, 2 );
			else if( ( humidityAndPrecip / 2 ) <= 40 )
				delta->cloudcover	+= ( cell->humidity / 5 );

			
			int totalPressure = cell->pressure;
			int numPressureCells = 1;
			//  Changes applied based on what is going on in adjacent cells
			int dx, dy;
			for( dy = -1; dy <= 1; ++dy )
			{
				for( dx = -1; dx <= 1; ++dx )
				{
					int nx = x + dx;
					int ny = y + dy;

					//  Skip THIS cell
					if( dx == 0 && dy == 0 )
						continue;

					//  Prevent array over/underruns
					if( nx < 0 || nx >= WEATHER_SIZE_X )
						continue;
					if( ny < 0 || ny >= WEATHER_SIZE_Y )
						continue;

					struct WeatherCell *neighborCell = &weatherMap[nx][ny];
					struct WeatherCell *neighborDelta = &weatherDelta[nx][ny];

					/*
					 *  We'll apply wind changes here
					 *  Wind speeds up in a given direction based on pressure

					 *  1/4 of the pressure difference applied to wind speed

					 *  Wind should move from higher pressure to lower pressure
					 *  and some of our pressure difference should go with it!
					 *  If we are pressure 60, and they are pressure 40
					 *  then with a difference of 20, lets make that a 4 mph
					 *  wind increase towards them!
					 *  So if they are west neighbor (dx < 0)
					 */

					int	pressureDelta = cell->pressure - neighborCell->pressure;
					int windSpeedDelta = pressureDelta / 4;

					if( dx != 0 )		//	Neighbor to east or west
						delta->windSpeedX += ( windSpeedDelta * dx );	//	dx = -1 or 1
					if( dy != 0 )		//	Neighbor to north or south
						delta->windSpeedY += ( windSpeedDelta * dy );	//	dy = -1 or 1

					totalPressure += neighborCell->pressure; ++numPressureCells;

					//  Now GIVE them a bit of temperature and humidity change
					//  IF our wind is blowing towards them
					int temperatureDelta =  ( cell->temperature - neighborCell->temperature );
					temperatureDelta /= 16;

					int humidityDelta = cell->humidity - neighborCell->humidity;
					humidityDelta /= 16;
					

					if(   ( cell->windSpeedX < 0 && dx < 0 )
						|| ( cell->windSpeedX > 0 && dx > 0 )
						|| ( cell->windSpeedY < 0 && dy < 0 )
						|| ( cell->windSpeedY > 0 && dy > 0 ) )
					{
						neighborDelta->temperature += temperatureDelta; 
                        neighborDelta->humidity += humidityDelta;
                        delta->temperature -= temperatureDelta; 
                        delta->humidity -= humidityDelta;
					}
					// Determine change in the energy of this particular Cell
					int energyDelta = number_range( -10, 10 );
					delta->energy += energyDelta;
				}
			}
			//  Subtract because if positive means we are higher
			  delta->pressure = ( ( totalPressure / numPressureCells ) - cell->pressure );

			 /* 
			  * Precipitation should screw with pressure to keep the system from
			  * reaching a balancing point.
			  */
			if( cell->precipitation >= 70 ) 
				delta->pressure -= ( cell->pressure / 2 );
			else if( cell->precipitation < 70 && cell->precipitation > 30 )
				delta->pressure += number_range( -5, 5 );
			else if( cell->precipitation <= 30 )
				delta->pressure += ( cell->pressure / 2 );
		}
	}
}

void EnforceClimateConditions( void )
{
	int x, y;
	
	/*
	 * This function is used to enforce certain conditions to be upheld
	 * within cells.  The cells should have a climate set to them, which
	 * tells this function which conditions to enforce. Conditions are pretty
	 * straight forward, and tend to mesh with climate conditions around
	 * Earth.
	 */
	for( y = 0; y < WEATHER_SIZE_Y; y++ )
	{
		for( x = 0; x < WEATHER_SIZE_X; x++ )
		{
		
			struct	WeatherCell *cell = &weatherMap[x][y];    //  Weather cell
			struct	WeatherCell *delta = &weatherDelta[x][y];

			if( cell->climate == CLIMATE_RAINFOREST )  
			{
				if(  cell->temperature < 80 ) 
					delta->temperature += 3;
				else if( cell->humidity < 50 )
					delta->humidity += 2;
			}
			else if( cell->climate == CLIMATE_SAVANNA )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					    && cell->humidity > 0 ) 
					delta->humidity += -5;
				else if( cell->temperature < 60  )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					    && cell->humidity < 50 ) 
					delta->humidity += 5;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					    && cell->humidity > 0 ) 
					delta->humidity += -5;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					    && cell->humidity < 50 ) 
					delta->humidity += 5;
			}
			else if( cell->climate == CLIMATE_DESERT )  
			{
				if( ( time_info.sunlight == SUN_SET || time_info.sunlight == SUN_DARK ) && cell->temperature > 30 ) 
					delta->temperature += -5;
				else if( ( time_info.sunlight == SUN_RISE || time_info.sunlight == SUN_LIGHT ) && cell->temperature < 64 )
					delta->temperature += 2;
				else if(  cell->humidity > 10 ) 
					delta->humidity += -2;
				else if(  cell->pressure < 50 ) 
					delta->pressure += 2;
			}
			else if( cell->climate == CLIMATE_STEPPE )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					   && cell->temperature > 50 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					   && cell->temperature < 50 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					   && cell->temperature > 50 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					   && cell->temperature < 50 )
					delta->temperature += 2;
				else if( cell->humidity > 60 ) 
					delta->temperature += -2;
				else if( cell->humidity < 30 ) 
					delta->temperature += 2;
			}
			else if( cell->climate == CLIMATE_CHAPPARAL )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					    && cell->temperature > 50  ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					    && cell->temperature < 75  )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					    && cell->humidity > 30 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					    && cell->humidity < 30 ) 
					delta->humidity += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					    && cell->temperature > 50  ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					    && cell->temperature < 75  )
					delta->temperature += 2;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					    && cell->humidity > 30 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					    && cell->humidity < 30 ) 
					delta->humidity += 2;
			}
			else if( cell->climate == CLIMATE_GRASSLANDS )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					   && cell->temperature > 50  ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					   && cell->temperature < 75  )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					   && cell->humidity > 40 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					   && cell->humidity < 30 ) 
					delta->humidity += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					   && cell->temperature > 50  ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					   && cell->temperature < 75  )
					delta->temperature += 2;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					   && cell->humidity > 40 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					   && cell->humidity < 30 ) 
					delta->humidity += 2;
			}
			else if( cell->climate == CLIMATE_DECIDUOUS )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->temperature > 40  ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->temperature < 65  )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->temperature > 40  ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->temperature < 65  )
					delta->temperature += 2;
				else if(  cell->humidity < 30 ) 
					delta->humidity += 2;
			}
			else if( cell->climate == CLIMATE_TAIGA )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->temperature > 30 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->temperature < 40 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->temperature > 30 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->temperature > 30 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->humidity < 50 ) 
					delta->humidity += 2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->humidity > 20 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->humidity > 20 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_NORTH
					  && cell->humidity > 20 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->temperature > 30 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->temperature < 40 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 30 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->temperature > 30 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->humidity < 50 ) 
					delta->humidity += 2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->humidity > 20 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->humidity > 20 ) 
					delta->humidity += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_SOUTH
					  && cell->humidity > 20 ) 
					delta->humidity += -2;
			}
			else if( cell->climate == CLIMATE_TUNDRA )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					 && cell->temperature > 20 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					 && cell->temperature < 50 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_NORTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_NORTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 20 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature < 50 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
			}
			else if( cell->climate == CLIMATE_ALPINE )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH
					&& cell->temperature > 20 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH
					&& cell->temperature > 50 )
					delta->temperature += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_NORTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_NORTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 20 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 50 )
					delta->temperature += -2;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_SOUTH
					 && cell->temperature > 25 ) 
					delta->temperature += -2;
			}
			else if( cell->climate == CLIMATE_ARCTIC )  
			{
				if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_NORTH 
					  && cell->temperature > -10 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_WINTER && cell->hemisphere == HEMISPHERE_SOUTH 
					  && cell->temperature > -10 ) 
					delta->temperature += 3;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_NORTH 
					  && cell->temperature > -10 ) 
					delta->temperature += -3;
				else if( time_info.season == SEASON_FALL && cell->hemisphere == HEMISPHERE_SOUTH 
					  && cell->temperature > -10 ) 
					delta->temperature += 3;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH 
					  && cell->temperature < -5 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH 
					  && cell->temperature < -5 )
					delta->temperature += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_NORTH 
					  && cell->temperature > 10 )
					delta->temperature += -2;
				else if( time_info.season == SEASON_SUMMER && cell->hemisphere == HEMISPHERE_SOUTH 
					  && cell->temperature > 10 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_NORTH 
					  && cell->temperature < -5 )
					delta->temperature += 2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_SOUTH 
					  && cell->temperature < -5 )
					delta->temperature += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_NORTH 
					  && cell->temperature > 10 )
					delta->temperature += -2;
				else if( time_info.season == SEASON_SPRING && cell->hemisphere == HEMISPHERE_SOUTH 
					  && cell->temperature > 10 )
					delta->temperature += 2;
				
				if( time_info.season == SEASON_WINTER ) 
					delta->humidity += -1;
				else if( time_info.season == SEASON_FALL ) 
					delta->humidity += 1;
				else if( time_info.season == SEASON_SUMMER )
					delta->humidity += 2;
				else if( time_info.season == SEASON_SPRING )
					delta->humidity += -2;
			}
		}
	}
}


void UpdateWeather( void )
{
     ClearWeatherDeltas(  );
     CalculateCellToCellChanges(  );
     EnforceClimateConditions(  );
     ApplyDeltaChanges(  );
	 save_weathermap(  );
}

void RandomizeCells( void )
{
	int x, y;
	/*
	 * This function came about because of the inexplicable ability
	 * of the system, to find its way around anything I coded, and
	 * still manage to completely throw itself off based on a single
	 * value reaching the Max or Min for that value.
	 * What this does:
	 * Every night at midnight(as per the single call to this function
	 * in time_update()) It will randomize the values in each cell, 
	 * based on hemisphere, climate, and season. 
	 */
	for( y = 0; y < WEATHER_SIZE_Y; y++ )
	{
		for( x = 0; x < WEATHER_SIZE_X; x++ )
		{
		
		struct	WeatherCell *cell = &weatherMap[x][y];    //  Weather cell

			if( cell->hemisphere == HEMISPHERE_NORTH )
			{
				if( time_info.season == SEASON_SPRING )
				{
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 20, 40 );	
						cell->humidity		= number_range( 60, 80 );	
						cell->precipitation	= number_range( 60, 80 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 40, 70 );	
						cell->pressure		= number_range( 20, 40 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 50, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{
						cell->temperature	= number_range( 30, 50 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )  
					{
						cell->temperature	= number_range( 45, 65 );	
						cell->pressure		= number_range( 20, 40 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( 0, 30 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( 10, 40 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{
						cell->temperature	= number_range( 20, 50 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( 0, 20 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
				}
				else if( time_info.season == SEASON_SUMMER )
				{
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 20, 40 );	
						cell->humidity		= number_range( 60, 80 );	
						cell->precipitation	= number_range( 60, 80 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{
						cell->temperature	= number_range( 80, 100 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 80, 100 );	
						cell->pressure		= number_range( 50, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )  
					{
						cell->temperature	= number_range( 65, 95 );	
						cell->pressure		= number_range( 60, 90 );	
						cell->cloudcover    = number_range( 10, 30 );	
						cell->humidity		= number_range( 10, 30 );	
						cell->precipitation	= number_range( 10, 30 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( 30, 70 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 20, 50 );	
						cell->humidity		= number_range( 20, 50 );	
						cell->precipitation	= number_range( 20, 50 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( 40, 70 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{
						cell->temperature	= number_range( 30, 60 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( -30, -10 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
				}
				else if( time_info.season == SEASON_FALL )
				{
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 60, 80 );	
						cell->cloudcover    = number_range( 10, 30 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 40, 70 );	
						cell->pressure		= number_range( 20, 40 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 30, 70 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 20, 40 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{
						cell->temperature	= number_range( 30, 50 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )  
					{
						cell->temperature	= number_range( 55, 75 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 40, 60 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( 0, 30 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}	
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( 10, 40 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 20, 60 );	
						cell->humidity		= number_range( 20, 60 );	
						cell->precipitation	= number_range( 20, 60 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{	
						cell->temperature	= number_range( 20, 50 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( 0, 20 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}	
				}
				else if( time_info.season == SEASON_WINTER )
				{	
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 60, 80 );	
						cell->cloudcover    = number_range( 10, 30 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{	
						cell->temperature	= number_range( 50, 70 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( -10, 20 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 40, 60 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 60, 80 );	
						cell->precipitation	= number_range( 40, 60 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{	
						cell->temperature	= number_range( -30, 0 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )	
					{
						cell->temperature	= number_range( 10, 30 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 40, 60 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( -30, 0 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( -10, 20 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 20, 60 );	
						cell->humidity		= number_range( 20, 60 );	
						cell->precipitation	= number_range( 20, 60 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{
						cell->temperature	= number_range( -30, 10 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( 30, 60 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
				}
			}
			else
			{
				if( time_info.season == SEASON_SPRING )
				{
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{	
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 60, 80 );	
						cell->cloudcover    = number_range( 10, 30 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 40, 70 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 30, 70 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 20, 40 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{
						cell->temperature	= number_range( 30, 50 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )  
					{
						cell->temperature	= number_range( 55, 75 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 40, 60 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( 0, 30 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( 10, 40 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 20, 60 );	
						cell->humidity		= number_range( 20, 60 );	
						cell->precipitation	= number_range( 20, 60 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{
						cell->temperature	= number_range( 20, 50 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( 0, 20 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
				}
				else if( time_info.season == SEASON_SUMMER )
				{
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 60, 80 );	
						cell->cloudcover    = number_range( 10, 30 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{
						cell->temperature	= number_range( 80, 100 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 40, 60 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 60, 80 );	
						cell->precipitation	= number_range( 40, 60 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 50, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{
						cell->temperature	= number_range( -30, 0 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )  
					{
						cell->temperature	= number_range( 10, 30 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 40, 60 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( -30, 0 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( -10, 20 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 20, 60 );	
						cell->humidity		= number_range( 20, 60 );	
						cell->precipitation	= number_range( 20, 60 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{
						cell->temperature	= number_range( -30, 10 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{	
						cell->temperature	= number_range( 30, 60 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
				}
				else if( time_info.season == SEASON_FALL )
				{	
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 20, 40 );	
						cell->humidity		= number_range( 60, 80 );	
						cell->precipitation	= number_range( 60, 80 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 40, 70 );	
						cell->pressure		= number_range( 20, 40 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( -10, 20 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{
						cell->temperature	= number_range( 30, 50 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )  
					{
						cell->temperature	= number_range( 45, 65 );	
						cell->pressure		= number_range( 20, 40 );	
						cell->cloudcover    = number_range( 40, 60 );	
						cell->humidity		= number_range( 40, 60 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( 0, 30 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}	
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( 10, 40 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{	
						cell->temperature	= number_range( 20, 50 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( 0, 20 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}	
				}
				else if( time_info.season == SEASON_WINTER )
				{	
					if( cell->climate == CLIMATE_RAINFOREST )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 30, 60 );	
						cell->cloudcover    = number_range( 50, 70 );	
						cell->humidity		= number_range( 70, 100 );	
						cell->precipitation	= number_range( 70, 100 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_SAVANNA )  
					{
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 20, 40 );	
						cell->humidity		= number_range( 60, 80 );	
						cell->precipitation	= number_range( 60, 80 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DESERT )  
					{	
						cell->temperature	= number_range( 50, 70 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 10 );	
						cell->precipitation	= number_range( 0, 10 );	
						cell->windSpeedX	= number_range( -10, 10 );	
						cell->windSpeedY	= number_range( -10, 10 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_STEPPE )  
					{
						cell->temperature	= number_range( 70, 90 );	
						cell->pressure		= number_range( 70, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -30, 30 );	
						cell->windSpeedY	= number_range( -30, 30 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_CHAPPARAL )  
					{
						cell->temperature	= number_range( 80, 100 );	
						cell->pressure		= number_range( 50, 90 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_GRASSLANDS )  
					{	
						cell->temperature	= number_range( 60, 80 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_DECIDUOUS )	
					{
						cell->temperature	= number_range( 65, 95 );	
						cell->pressure		= number_range( 60, 90 );	
						cell->cloudcover    = number_range( 10, 30 );	
						cell->humidity		= number_range( 10, 30 );	
						cell->precipitation	= number_range( 10, 30 );	
						cell->windSpeedX	= number_range( -40, 40 );	
						cell->windSpeedY	= number_range( -40, 40 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TAIGA )  
					{
						cell->temperature	= number_range( 30, 70 );	
						cell->pressure		= number_range( 40, 60 );	
						cell->cloudcover    = number_range( 20, 50 );	
						cell->humidity		= number_range( 20, 50 );	
						cell->precipitation	= number_range( 20, 50 );	
						cell->windSpeedX	= number_range( -50, 50 );	
						cell->windSpeedY	= number_range( -50, 50 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_TUNDRA )  
					{
						cell->temperature	= number_range( 40, 70 );	
						cell->pressure		= number_range( 80, 100 );	
						cell->cloudcover    = number_range( 0, 20 );	
						cell->humidity		= number_range( 0, 20 );	
						cell->precipitation	= number_range( 0, 20 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ALPINE )  
					{
						cell->temperature	= number_range( 30, 60 );	
						cell->pressure		= number_range( 30, 50 );	
						cell->cloudcover    = number_range( 60, 80 );	
						cell->humidity		= number_range( 50, 70 );	
						cell->precipitation	= number_range( 50, 70 );	
						cell->windSpeedX	= number_range( -60, 60 );	
						cell->windSpeedY	= number_range( -60, 60 );
						cell->energy		= number_range( 0, 100 );
					}
					else if( cell->climate == CLIMATE_ARCTIC )  
					{
						cell->temperature	= number_range( -30, -10 );	
						cell->pressure		= number_range( 85, 100 );	
						cell->cloudcover    = number_range( 0, 15 );	
						cell->humidity		= number_range( 0, 15 );	
						cell->precipitation	= number_range( 0, 15 );	
						cell->windSpeedX	= number_range( -20, 20 );	
						cell->windSpeedY	= number_range( -20, 20 );
						cell->energy		= number_range( 0, 100 );
					}
				}
			}
		}
	}
}

void save_weathermap( void )
{
   int x, y;
   char filename[MIL];
   FILE *fp;

   snprintf( filename, MIL, "%s%s", SYSTEM_DIR, WEATHER_FILE );
   if( !( fp = fopen( filename, "w" ) ) )
   {
      bug( "%s: fopen", __FUNCTION__ );
      perror( filename );
      return;
   }
   
   for ( y = 0; y < WEATHER_SIZE_Y; y++)
   {
     for ( x = 0; x < WEATHER_SIZE_X; x++)
	 {
	    struct	WeatherCell *cell = &weatherMap[x][y];

		fprintf( fp, "#CELL		  %d %d\n", x, y );
		fprintf( fp, "Climate     %d\n", cell->climate );
		fprintf( fp, "Hemisphere  %d\n", cell->hemisphere ); 
		fprintf( fp, "State       %d %d %d %d %d %d %d %d\n", cell->cloudcover, cell->energy, cell->humidity, 
			cell->precipitation, cell->pressure, cell->temperature, cell->windSpeedX, cell->windSpeedY );
		fprintf( fp, "End\n\n" );
	 }
   }
   fprintf( fp, "\n#END\n\n" );
   fclose( fp );
   fp = NULL;
   return;
}

void fread_cell( FILE * fp, int x, int y )
{
   bool fMatch = FALSE;

   struct WeatherCell *cell = &weatherMap[x][y];

   for( ;; )
   {
      const char *word = feof( fp ) ? "End" : fread_word( fp );

      switch ( UPPER( word[0] ) )
      {
         case '*':
            fread_to_eol( fp );
            break;

		 case 'C':
			 KEY( "Climate", cell->climate, fread_number( fp ) );
			 break;

		 case 'E':
             if( !str_cmp( word, "End" ) )
               return;
             break;

		 case 'H':
			 KEY( "Hemisphere", cell->hemisphere, fread_number( fp ) );
			 break;

		 case 'S':
			if( !str_cmp( word, "State" ) )
            {
              cell->cloudcover = fread_number( fp );
			  cell->energy = fread_number( fp );
              cell->humidity = fread_number( fp );
              cell->precipitation = fread_number( fp );
              cell->pressure = fread_number( fp );
			  cell->temperature = fread_number( fp );
			  cell->windSpeedX = fread_number( fp );
			  cell->windSpeedY = fread_number( fp );
			  fMatch = TRUE;
			  break;
            }
            break;
      }
      if( !fMatch )
      {
         bug( "%s: no match for %s", __FUNCTION__, word );
         fread_to_eol( fp );
      }
   }
}


bool load_weathermap( void )
{
   FILE *fp = NULL;
   char filename[256];
   int x, y;

   snprintf( filename, 256, "%s%s", SYSTEM_DIR, WEATHER_FILE );
   if( !( fp = fopen( filename, "r" ) ) )
   {
      bug( "load_weathermap(): cannot open %s for reading", filename );
      return FALSE;
   }
   for( ;; )
   {
      char letter = fread_letter( fp );
      char *word;

      if( letter == '*' )
      {
         fread_to_eol( fp );
         continue;
      }

      if( letter != '#' )
      {
         bug( "%s: # not found (%c)", __FUNCTION__, letter );
         return FALSE;
      }

      word = fread_word( fp );
      if( !str_cmp( word, "CELL" ) ) 
      { 
        x = fread_number( fp );
		y = fread_number( fp );
		fread_cell( fp, x, y );
		continue;
      }
      else if( !str_cmp( word, "END" ) )
         break;
      else
      {
         bug( "%s: no match for %s", __FUNCTION__, word );
         continue;
      }
   }
   fclose( fp );
   fp = NULL;
   return TRUE;
}

/*
 * Weather Utility Functions
 * Designed to attempt to emulate encapsulation.
 */
struct WeatherCell *getWeatherCell( AREA_DATA *pArea  )
{
   return &weatherMap[pArea->weatherx][pArea->weathery];
}	

void IncreaseTemp( struct WeatherCell *cell, int change )
{
	cell->temperature += change;
}

void DecreaseTemp( struct WeatherCell *cell, int change )
{
	cell->temperature -= change;
}

void IncreasePrecip( struct WeatherCell *cell, int change )
{
	cell->precipitation += change;
}

void DecreasePrecip( struct WeatherCell *cell, int change )
{
	cell->precipitation -= change;
}

void IncreasePressure( struct WeatherCell *cell, int change )
{
	cell->pressure += change;
}

void DecreasePressure( struct WeatherCell *cell, int change )
{
	cell->pressure -= change;
}

void IncreaseEnergy( struct WeatherCell *cell, int change )
{
	cell->energy += change;
}

void DecreaseEnergy( struct WeatherCell *cell, int change )
{
	cell->energy -= change;
}

void IncreaseCloudCover( struct WeatherCell *cell, int change )
{
	cell->cloudcover += change;
}

void DecreaseCloudCover( struct WeatherCell *cell, int change )
{
	cell->cloudcover -= change;
}

void IncreaseHumidity( struct WeatherCell *cell, int change )
{
	cell->humidity += change;
}

void DecreaseHumidity( struct WeatherCell *cell, int change )
{
	cell->humidity -= change;
}

void IncreaseWindX( struct WeatherCell *cell, int change )
{
	cell->windSpeedX += change;
}

void DecreaseWindX( struct WeatherCell *cell, int change )
{
	cell->windSpeedX -= change;
}

void IncreaseWindY( struct WeatherCell *cell, int change )
{
	cell->windSpeedY += change;
}

void DecreaseWindY( struct WeatherCell *cell, int change )
{
	cell->windSpeedY -= change;
}



/* Cloud cover Information */
int getCloudCover( struct WeatherCell *cell )
{
	return cell->cloudcover; 
}

bool isExtremelyCloudy( int cloudCover )
{
  if( cloudCover > 80 )
	return TRUE;
  else 
	return FALSE;
}

bool isModeratelyCloudy( int cloudCover )
{
	if( cloudCover > 60 && cloudCover <= 80 )
		return TRUE;
	else 
		return FALSE;
}

bool isPartlyCloudy( int cloudCover )
{
	if( cloudCover > 40 && cloudCover <= 60 )
		return TRUE;
	else
		return FALSE;
}

bool isCloudy( int cloudCover )
{
	if( cloudCover > 20 && cloudCover <= 40 )
		return TRUE;
	else
		return FALSE;
}

/* Temperature Information */
int getTemp( struct WeatherCell *cell )
{
	return cell->temperature; 
}

bool isSwelteringHeat( int temp )
{
  if( temp > 90 )
	return TRUE;
  else 
	return FALSE;
}

bool isVeryHot( int temp )
{
	if( temp > 80 && temp <= 90 )
		return TRUE;
	else 
		return FALSE;
}

bool isHot( int temp )
{
	if( temp > 70 && temp <= 80 )
		return TRUE;
	else
		return FALSE;
}

bool isWarm( int temp )
{
	if( temp > 60 && temp <= 70 )
		return TRUE;
	else
		return FALSE;
}

bool isTemperate( int temp )
{
	if( temp > 50 && temp <= 60 )
		return TRUE;
	else 
		return FALSE;
}

bool isCool( int temp )
{
	if( temp > 40 && temp <= 50 )
		return TRUE;
	else
		return FALSE;
}

bool isChilly( int temp )
{
	if( temp > 30 && temp <= 40 )
		return TRUE;
	else
		return FALSE;
}

bool isCold( int temp )
{
	if( temp > 20 && temp <= 30 )
		return TRUE;
	else 
		return FALSE;
}

bool isFrosty( int temp )
{
	if( temp > 10 && temp <= 20 )
		return TRUE;
	else
		return FALSE;
}

bool isFreezing( int temp )
{
	if( temp > 0 && temp <= 10 )
		return TRUE;
	else
		return FALSE;
}

bool isReallyCold( int temp )
{
	if( temp > -10 && temp <= 0 )
		return TRUE;
	else 
		return FALSE;
}

bool isVeryCold( int temp )
{
	if( temp > -20 && temp <= -10 )
		return TRUE;
	else
		return FALSE;
}

bool isExtremelyCold( int temp )
{
	if( temp <= -20 )
		return TRUE;
	else
		return FALSE;
}

/* Energy Information */
int getEnergy( struct WeatherCell *cell )
{
	return cell->energy; 
}

bool isStormy( int energy )
{
  if( energy > 50 )
	return TRUE;
  else 
	return FALSE;
}

/* Pressure Information */
int getPressure( struct WeatherCell *cell )
{
	return cell->pressure; 
}

bool isHighPressure( int pressure )
{
  if( pressure > 50 )
	return TRUE;
  else 
	return FALSE;
}


bool isLowPressure( int pressure )
{
	if( pressure < 50 )
		return TRUE;
	else
		return FALSE;
}

/* Humidity Information */
int getHumidity( struct WeatherCell *cell )
{
	return cell->humidity; 
}

bool isExtremelyHumid( int humidity )
{
  if( humidity > 80 )
	return TRUE;
  else 
	return FALSE;
}

bool isModeratelyHumid( int humidity )
{
	if( humidity > 60 && humidity < 80 )
		return TRUE;
	else 
		return FALSE;
}

bool isMinorlyHumid( int humidity )
{
	if( humidity > 40 && humidity < 60 )
		return TRUE;
	else
		return FALSE;
}

bool isHumid( int humidity )
{
	if( humidity > 20 && humidity < 40 )
		return TRUE;
	else
		return FALSE;
}

/* Precipitation Information */
int getPrecip( struct WeatherCell *cell )
{
	return cell->precipitation; 
}

bool isTorrentialDownpour( int precip )
{
  if( precip > 90 )
	return TRUE;
  else 
	return FALSE;
}

bool isRainingCatsAndDogs( int precip )
{
	if( precip > 80 && precip <= 90 )
		return TRUE;
	else 
		return FALSE;
}

bool isPouring( int precip )
{
	if( precip > 70 && precip <= 80 )
		return TRUE;
	else
		return FALSE;
}

bool isRaingingHeavily( int precip )
{
	if( precip > 60 && precip <= 70 )
		return TRUE;
	else
		return FALSE;
}

bool isDownpour( int precip )
{
	if( precip > 50 && precip <= 60 )
		return TRUE;
	else 
		return FALSE;
}

bool isRainingSteadily( int precip )
{
	if( precip > 40 && precip <= 50 )
		return TRUE;
	else
		return FALSE;
}

bool isRaining( int precip )
{
	if( precip > 30 && precip <= 40 )
		return TRUE;
	else
		return FALSE;
}

bool isRainingLightly( int precip )
{
	if( precip > 20 && precip <= 30 )
		return TRUE;
	else 
		return FALSE;
}

bool isDrizzling( int precip )
{
	if( precip > 10 && precip <= 20 )
		return TRUE;
	else
		return FALSE;
}

bool isMisting( int precip )
{
	if( precip > 0 && precip <= 10 )
		return TRUE;
	else
		return FALSE;
}

/* WindX Information */
int getWindX( struct WeatherCell *cell )
{
	return cell->windSpeedX; 
}

bool isCalmWindE( int windx )
{
  if( windx > 0 && windx <= 10 )
	return TRUE;
  else 
	return FALSE;
}

bool isBreezyWindE( int windx )
{
	if( windx > 10 && windx <= 20 )
		return TRUE;
	else 
		return FALSE;
}

bool isBlusteryWindE( int windx )
{
	if( windx > 20 && windx <= 40 )
		return TRUE;
	else
		return FALSE;
}

bool isWindyWindE( int windx )
{
	if( windx > 40 && windx <= 60 )
		return TRUE;
	else
		return FALSE;
}

bool isGustyWindE( int windx )
{
	if( windx > 60 && windx <= 80 )
		return TRUE;
	else
		return FALSE;
}

bool isGaleForceWindE( int windx )
{
	if( windx > 80 && windx <= 100 )
		return TRUE;
	else
		return FALSE;
}

bool isCalmWindW( int windx )
{
  if( windx < 0 && windx >= -10 )
	return TRUE;
  else 
	return FALSE;
}

bool isBreezyWindW( int windx )
{
	if( windx < -10 && windx >= -20 )
		return TRUE;
	else 
		return FALSE;
}

bool isBlusteryWindW( int windx )
{
	if( windx < -20 && windx >= -40 )
		return TRUE;
	else
		return FALSE;
}

bool isWindyWindW( int windx )
{
	if( windx < -40 && windx >= -60 )
		return TRUE;
	else
		return FALSE;
}

bool isGustyWindW( int windx )
{
	if( windx < -60 && windx >= -80 )
		return TRUE;
	else
		return FALSE;
}

bool isGaleForceWindW( int windx )
{
	if( windx < -80 && windx >= -100 )
		return TRUE;
	else
		return FALSE;
}

/* WindY Information */
int getWindY( struct WeatherCell *cell )
{
	return cell->windSpeedY; 
}

bool isCalmWindN( int windy )
{
  if( windy > 0 && windy <= 10 )
	return TRUE;
  else 
	return FALSE;
}

bool isBreezyWindN( int windy )
{
	if( windy > 10 && windy <= 20 )
		return TRUE;
	else 
		return FALSE;
}

bool isBlusteryWindN( int windy )
{
	if( windy > 20 && windy <= 40 )
		return TRUE;
	else
		return FALSE;
}

bool isWindyWindN( int windy )
{
	if( windy > 40 && windy <= 60 )
		return TRUE;
	else
		return FALSE;
}

bool isGustyWindN( int windy )
{
	if( windy > 60 && windy <= 80 )
		return TRUE;
	else
		return FALSE;
}

bool isGaleForceWindN( int windy )
{
	if( windy > 80 && windy <= 100 )
		return TRUE;
	else
		return FALSE;
}

bool isCalmWindS( int windy )
{
  if( windy < 0 && windy >= -10 )
	return TRUE;
  else 
	return FALSE;
}

bool isBreezyWindS( int windy )
{
	if( windy < -10 && windy >= -20 )
		return TRUE;
	else 
		return FALSE;
}

bool isBlusteryWindS( int windy )
{
	if( windy < -20 && windy >= -40 )
		return TRUE;
	else
		return FALSE;
}

bool isWindyWindS( int windy )
{
	if( windy < -40 && windy >= -60 )
		return TRUE;
	else
		return FALSE;
}

bool isGustyWindS( int windy )
{
	if( windy < -60 && windy >= -80 )
		return TRUE;
	else
		return FALSE;
}

bool isGaleForceWindS( int windy )
{
	if( windy < -80 && windy >= -100 )
		return TRUE;
	else
		return FALSE;
} 


void do_setweather( CHAR_DATA* ch, const char* argument)
{
   char arg[MIL], arg2[MIL], arg3[MIL], arg4[MIL];
   int value, x, y;

   argument = one_argument( argument, arg );
   argument = one_argument( argument, arg2 );
   argument = one_argument( argument, arg3 );
   argument = one_argument( argument, arg4 );

   if( IS_NPC( ch ) )
   {
      send_to_char( "Mob's can't setweather.\r\n", ch );
      return;
   }

   if( !ch->desc )
   {
      send_to_char( "Nice try, but You have no descriptor.\r\n", ch );
      return;
   }
	
   if( arg[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0' )
   {
      send_to_char( "Syntax: setweather <x> <y> <field> <value>\r\n", ch );
      send_to_char( "\r\n", ch );
      send_to_char( "Field being one of:\r\n", ch );
      send_to_char( "  climate hemisphere\r\n", ch );
      send_to_char( "Climate value being:\r\n", ch );
      send_to_char( "  rainforest savanna desert steppe chapparal arctic\r\n", ch );
      send_to_char( "  grasslands deciduous_forest taiga tundra alpine\r\n", ch );
      send_to_char( " See Help Climates for information on each.\r\n", ch );
      send_to_char( "Hemisphere value being:\r\n", ch );
      send_to_char( "  northern southern\r\n", ch );
      return;
   }

   x = atoi( arg );
   y = atoi( arg2 );

   if( x < 0 || x > WEATHER_SIZE_X )
   {
      ch_printf( ch, "X value must be between 0 and %d.\r\n", WEATHER_SIZE_X );
      return;
   }

   if( y < 0 || y > WEATHER_SIZE_Y )
   {
      ch_printf( ch, "Y value must be between 0 and %d.\r\n", WEATHER_SIZE_Y );
      return;
   }

   struct WeatherCell *cell = &weatherMap[x][y];

   if( !str_cmp( arg3, "climate" ) )
   {
      if( arg4[0] == '\0' )
      {
         send_to_char( "Usage: setweather <x> <y> climate <flag>\r\n", ch );
         return;
      }

      value = get_climate( arg4 );
      if( value < 0 || value > MAX_CLIMATE )
         ch_printf( ch, "Unknown flag: %s\r\n", arg4 );
      else
      {
         cell->climate = 0;
         SET_BIT( cell->climate, 1 << value );
         send_to_char( "Cell Climate set.\r\n", ch );
      }
      return;
   }

   if( !str_cmp( arg3, "hemisphere" ) )
   {
      if( arg4[0] == '\0' )
      {
         send_to_char( "Usage: setweather <x> <y> hemisphere <flag>\r\n", ch );
         return;
      }

      value = get_hemisphere( arg4 );
      if( value < 0 || value > HEMISPHERE_MAX )
         ch_printf( ch, "Unknown flag: %s\r\n", arg4 );
      else
      {
         cell->hemisphere = 0;
         SET_BIT( cell->hemisphere, 1 << value );
         send_to_char( "Cell Hemisphere set.\r\n", ch );
      }
      return;
   }
   else
   {
      send_to_char( "Syntax: setweather <x> <y> <field> <value>\r\n", ch );
      send_to_char( "\r\n", ch );
      send_to_char( "Field being one of:\r\n", ch );
      send_to_char( "  climate hemisphere\r\n", ch );
      send_to_char( "Climate value being:\r\n", ch );
      send_to_char( "  rainforest savanna desert steppe chapparal arctic\r\n", ch );
      send_to_char( "  grasslands deciduous_forest taiga tundra alpine\r\n", ch );
      send_to_char( " See Help Climates for information on each.\r\n", ch );
      send_to_char( "Hemisphere value being:\r\n", ch );
      send_to_char( "  northern southern\r\n", ch );
      return;
   }
}

void do_showweather( CHAR_DATA* ch, const char* argument)
{
   char arg[MIL], arg2[MIL];
   int x, y;

   argument = one_argument( argument, arg );
   argument = one_argument( argument, arg2 );

   if( IS_NPC( ch ) )
   {
      send_to_char( "Mob's can't showweather.\r\n", ch );
      return;
   }

   if( !ch->desc )
   {
      send_to_char( "Nice try, but You have no descriptor.\r\n", ch );
      return;
   }
	
   if( arg[0] == '\0' || arg2[0] == '\0'  )
   {
      send_to_char( "Syntax: showweather <x> <y>\r\n", ch );
      return;
   }

   x = atoi( arg );
   y = atoi( arg2 );

   if( x < 0 || x > WEATHER_SIZE_X - 1 )
   {
      ch_printf( ch, "X value must be between 0 and %d.\r\n", WEATHER_SIZE_X - 1);
      return;
   }	

   if( y < 0 || y > WEATHER_SIZE_Y - 1 )
   {
      ch_printf( ch, "Y value must be between 0 and %d.\r\n", WEATHER_SIZE_Y - 1 );
      return;
   }

   struct WeatherCell *cell = &weatherMap[x][y];

   ch_printf_color( ch, "Current Weather State for:\r\n" );
   ch_printf_color( ch, "&WCell (&w%d&W, &w%d&W)&D\r\n", x, y );
   ch_printf_color( ch, "&WClimate:           &w%s&D\r\n", flag_string( cell->climate, climate_names ) );
   ch_printf_color( ch, "&WHemispere:         &w%s&D\r\n", flag_string( cell->hemisphere, hemisphere_name ) );
   ch_printf_color( ch, "&WCloud Cover:       &w%d&D\r\n", cell->cloudcover );
   ch_printf_color( ch, "&WEnergy:            &w%d&D\r\n", cell->energy );
   ch_printf_color( ch, "&WTemperature:       &w%d&D\r\n", cell->temperature );
   ch_printf_color( ch, "&WPressure:          &w%d&D\r\n", cell->pressure );
   ch_printf_color( ch, "&WHumidity:          &w%d&D\r\n", cell->humidity );
   ch_printf_color( ch, "&WPrecipitation:     &w%d&D\r\n", cell->precipitation );
   ch_printf_color( ch, "&WWind Speed XAxis:  &w%d&D\r\n", cell->windSpeedX );
   ch_printf_color( ch, "&WWind Speed YAxis:  &w%d&D\r\n", cell->windSpeedY );
}

void do_weather( CHAR_DATA* ch, const char* argument)
{
	struct WeatherCell *cell = getWeatherCell( ch->in_room->area );

	if( IS_NPC( ch ) )
    {
      send_to_char( "Mob's can't check the weather.\r\n", ch );
      return;
    }

    if( !ch->desc )
    {
      send_to_char( "Nice try, but You have no descriptor.\r\n", ch );
      return;
    }

	if( !IS_OUTSIDE( ch ) )
	{
		send_to_char( "You need to be outside to do that!\r\n", ch );
		return;
	}

	ch_printf_color( ch, "&wAs you check the weather around you, you notice:&D\r\n" );
	if( getPrecip( cell ) > 0 )
	{
		if( isTorrentialDownpour( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WThe snow is creating such a blizzard you can barely see!&D\r\n" );
			else
				ch_printf_color( ch, "&BThe rain is coming down in torrents!&D\r\n" );
            }
		else if( isRainingCatsAndDogs( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WThe snow is creating a near solid wall of white!&D\r\n" );
			else
				ch_printf_color( ch, "&BThe rain is coming down in big heavy drops!&D\r\n" );
            }
		else if( isPouring( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WThe snow is coming down hard.&D\r\n" );
			else
				ch_printf_color( ch, "&BThe rain is pouring down.&D\r\n" );
            }
		else if( isRaingingHeavily( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WThe snow falls heavily.&D\r\n" );
			else
				ch_printf_color( ch, "&BThe rain falls heavily.&D\r\n" );
            }
		else if( isDownpour( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WThe snow is coming down in heavy waves.&D\r\n" );
			else
				ch_printf_color( ch, "&BThe rain is coming down in sheets.&D\r\n" );
            }
		else if( isRainingSteadily( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WThe snow appears to be falling pretty steadily.&D\r\n" );
			else
				ch_printf_color( ch, "&BThe rain appears to be falling pretty steadily.&D\r\n" );
            }
		else if( isRaining( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WSnowflakes drift down from the heavens.&D\r\n" );
			else
				ch_printf_color( ch, "&BRain falls from the sky.&D\r\n" );
            }
		else if( isRainingLightly( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WA light snow falls around you.&D\r\n" );
			else
				ch_printf_color( ch, "&BA light rain patters on the ground around you.&D\r\n" );
            }
		else if( isDrizzling( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WSnow flurries about you.&D\r\n" );
			else
				ch_printf_color( ch, "&BA light drizzle seems to be falling.&D\r\n" );
            }
		else if( isMisting( getPrecip( cell ) ) )
            {
			if( getTemp( cell ) <= 32 )
				ch_printf_color( ch, "&WA few scattered snowflakes can be seen.&D\r\n" );
			else
				ch_printf_color( ch, "&BA light mist appears to be falling.&D\r\n" );
            }
	}
	else
		ch_printf_color( ch, "&BThere doesn't appear to be any form of precipitation.&D\r\n" );

	if( getCloudCover( cell ) > 0 )
	{
		if( isExtremelyCloudy( getCloudCover( cell ) ) )
			ch_printf_color( ch, "&wA blanket of clouds covers the sky.&D\r\n" );
		if( isModeratelyCloudy( getCloudCover( cell ) ) )
			ch_printf_color( ch, "&wThere looks to be a good bit of clouds in the sky.&D\r\n" );
		if( isPartlyCloudy( getCloudCover( cell ) ) )
			ch_printf_color( ch, "&wIt appears to be a partly cloudy sky.&D\r\n" );
		if( isCloudy( getCloudCover( cell ) ) )
			ch_printf_color( ch, "&wThere are a few scattered clouds in the sky.&D\r\n" );
	}
	else
		ch_printf_color( ch, "&wThere don't appear to be any clouds in the sky.&D\r\n" );

	if( getHumidity( cell ) > 0 )
	{
		if( isExtremelyHumid( getHumidity( cell ) ) )
			ch_printf_color( ch, "&cYour skin feels sickly sticky with the extreme humidity.&D\r\n" );
		else if( isModeratelyHumid( getHumidity( cell ) ) )
			ch_printf_color( ch, "&cYou feel slightly sticky because of the moderate humidity.&D\r\n" );
		else if( isMinorlyHumid( getHumidity( cell ) ) )
			ch_printf_color( ch, "&cThe stickyness of your skin is barely noticeable in the minor humidity.&D\r\n" );
		else if( isHumid( getHumidity( cell ) ) )
			ch_printf_color( ch, "&cThe air feels perfect against your skin.&D\r\n" );
		else
			ch_printf_color( ch, "&cYou can't feel a difference in the humidity.&D\r\n" );
	}
	else
		ch_printf_color( ch, "&cThe air seems as if to suck the moisture from your skin.&D\r\n" );

	if( getWindX( cell ) != 0 && getWindY( cell ) != 0 )
	{
		if( isCalmWindE( getWindX( cell ) ) && isCalmWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA calm wind brushes your skin from the southeast.&D\r\n" );
		else if( isCalmWindE( getWindX( cell ) ) && isCalmWindN( getWindY( cell ) ) ) 
			ch_printf_color( ch, "&GA calm wind brushes your skin from the northeast.&D\r\n" );
		else if( isBreezyWindE( getWindX( cell ) ) && isBreezyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the southeast.&D\r\n" );
		else if( isBreezyWindE( getWindX( cell ) ) && isBreezyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the northeast.&D\r\n" );
		else if( isBlusteryWindE( getWindX( cell ) ) && isBlusteryWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the southeast.&D\r\n" );
		else if( isBlusteryWindE( getWindX( cell ) ) && isBlusteryWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the northeast.&D\r\n" );
		else if( isWindyWindE( getWindX( cell ) ) && isWindyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the southeast.&D\r\n" );
		else if( isWindyWindE( getWindX( cell ) ) && isWindyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the northeast.&D\r\n" );
		else if( isGustyWindE( getWindX( cell ) ) && isGustyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the southeast.&D\r\n" );
		else if( isGustyWindE( getWindX( cell ) ) && isGustyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the northeast.&D\r\n" );
		else if( isGaleForceWindE( getWindX( cell ) ) && isGaleForceWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the southeast.&D\r\n" );
		else if( isGaleForceWindE( getWindX( cell ) ) && isGaleForceWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the northeast.&D\r\n" );
		
		else if( isCalmWindW( getWindX( cell ) ) && isCalmWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA calm wind brushes your skin from the southwest.&D\r\n" );
		else if( isCalmWindW( getWindX( cell ) ) && isCalmWindN( getWindY( cell ) ) ) 
			ch_printf_color( ch, "&GA calm wind brushes your skin from the northwest.&D\r\n" );
		else if( isBreezyWindW( getWindX( cell ) ) && isBreezyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the southwest.&D\r\n" );
		else if( isBreezyWindW( getWindX( cell ) ) && isBreezyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the northwest.&D\r\n" );
		else if( isBlusteryWindW( getWindX( cell ) ) && isBlusteryWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the southwest.&D\r\n" );
		else if( isBlusteryWindW( getWindX( cell ) ) && isBlusteryWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the northwest.&D\r\n" );
		else if( isWindyWindW( getWindX( cell ) ) && isWindyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the southwest.&D\r\n" );
		else if( isWindyWindW( getWindX( cell ) ) && isWindyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the northwest.&D\r\n" );
		else if( isGustyWindW( getWindX( cell ) ) && isGustyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the southwest.&D\r\n" );
		else if( isGustyWindW( getWindX( cell ) ) && isGustyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the northwest.&D\r\n" );
		else if( isGaleForceWindW( getWindX( cell ) ) && isGaleForceWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the southwest.&D\r\n" );
		else if( isGaleForceWindW( getWindX( cell ) ) && isGaleForceWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the northwest.&D\r\n" );

		else
			ch_printf_color( ch, "&GThe wind is blowing in such a chaotic manner, You can't tell where it's coming from!&D\r\n" );
	}
	else if( getWindX( cell ) != 0 && getWindY( cell ) == 0 )
	{
		if( isCalmWindE( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA calm wind brushes your skin from the east.&D\r\n" );
		else if( isBreezyWindE( getWindX( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the east.&D\r\n" );
		else if( isBlusteryWindE( getWindX( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the east.&D\r\n" );
		else if( isWindyWindE( getWindX( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the east.&D\r\n" );
		else if( isGustyWindE( getWindX( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the east.&D\r\n" );
		else if( isGaleForceWindE( getWindX( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the east.&D\r\n" );

		else if( isCalmWindW( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA calm wind brushes your skin from the west.&D\r\n" );
		else if( isBreezyWindW( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the west.&D\r\n" );
		else if( isBlusteryWindW( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the west.&D\r\n" );
		else if( isWindyWindW( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the west.&D\r\n" );
		else if( isGustyWindW( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the west.&D\r\n" );
		else if( isGaleForceWindW( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the west.&D\r\n" );
	}
	else if( getWindX( cell ) == 0 && getWindY( cell ) != 0 )
	{
		if( isCalmWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA calm wind brushes your skin from the south.&D\r\n" );
		else if( isBreezyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the south.&D\r\n" );
		else if( isBlusteryWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the south.&D\r\n" );
		else if( isWindyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the south.&D\r\n" );
		else if( isGustyWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the south.&D\r\n" );
		else if( isGaleForceWindS( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the south.&D\r\n" );
		
		else if( isCalmWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA calm wind brushes your skin from the north.&D\r\n" );
		else if( isBreezyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA steady breeze emanates from the north.&D\r\n" );
		else if( isBlusteryWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA blustery wind blows from the north.&D\r\n" );
		else if( isWindyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA strong steady wind howls from the north.&D\r\n" );
		else if( isGustyWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GThe wind seems to be coming in gusts from the north.&D\r\n" );
		else if( isGaleForceWindN( getWindY( cell ) ) )
			ch_printf_color( ch, "&GA gale force wind is tearing through the air from the north.&D\r\n" );
	}
	else
		ch_printf_color( ch, "&GThere doesn't seem to be any wind.\r\n" );

	if( getTemp( cell ) > -30 && getTemp( cell ) < 100 )
	{
		if( isSwelteringHeat( getTemp( cell ) ) )
			ch_printf_color( ch, "&OThe heat is almost unbearable.&D\r\n" );
		else if( isVeryHot( getTemp( cell ) ) )
			ch_printf_color( ch, "&OIt's very hot...&D\r\n" );
		else if( isHot( getTemp( cell ) ) )
			ch_printf_color( ch, "&OIt's hot...&D\r\n" );
		else if( isWarm( getTemp( cell ) ) )
			ch_printf_color( ch, "&OIt seems to be a bit warm.&D\r\n" );
		else if( isTemperate( getTemp( cell ) ) )
			ch_printf_color( ch, "&OThe temperature feels just right.&D\r\n" );
		else if( isCool( getTemp( cell ) ) )
			ch_printf_color( ch, "&CIt seems to be a bit cool.&D\r\n" );
		else if( isChilly( getTemp( cell ) ) )
			ch_printf_color( ch, "&CIt seems a bit chilly.&D\r\n" );
		else if( isCold( getTemp( cell ) ) )
			ch_printf_color( ch, "&CIt's cold.&D\r\n" );
		else if( isFrosty( getTemp( cell ) ) )
			ch_printf_color( ch, "&CThere is visible frost around.&D\r\n" );
		else if( isFreezing( getTemp( cell ) ) )
			ch_printf_color( ch, "&CYour breath seems to crystalize before your face.&D\r\n" );
		else if( isReallyCold( getTemp( cell ) ) )
			ch_printf_color( ch, "&CIt's really cold...&D\r\n" );
		else if( isVeryCold( getTemp( cell ) ) )
			ch_printf_color( ch, "&CYou think you see ice forming on your clothes.&D\r\n" );
		else if( isExtremelyCold( getTemp( cell ) ) )
			ch_printf_color( ch, "&CYou feel ice clinging to your skin. Get inside!&D\r\n" );
	}
}

# RDR2 Comprehensive Ped Database

## Overview
This database contains **1,504 total peds** from Red Dead Redemption 2, organized into 5 main categories for the Spawners UI.

## Database Structure

### Categories and Counts:
- **Humans**: 1,149 peds (NPCs, story characters, player models)
- **Animals**: 126 peds (wildlife and domestic animals)  
- **Horses**: 152 peds (horse breeds and variants)
- **Fish**: 56 peds (regular and legendary fish species)
- **Legendary Animals**: 21 peds (rare legendary animal variants)

## File Location
- **Main Database**: `rdr2_enhanced_ped_database.xml`
- **Backup/Working Files**: 
  - `rdr2_comprehensive_ped_database.xml` (original extraction)
  - `ped_database_builder.py` (initial builder script)
  - `extract_existing_peds.py` (extraction script)
  - `enhance_ped_database.py` (enhancement script)

## Database Features

### XML Structure
```xml
<PedDatabase version="1.0" game="RDR2" total_peds="1504">
  <Humans count="1149">
    <Ped model="CS_dutch" name="Dutch van der Linde"/>
    <Ped model="CS_johnmarston" name="John Marston"/>
    <!-- ... -->
  </Humans>
  
  <Animals count="126">
    <Ped model="A_C_Bear_01" name="American Black Bear" type="regular"/>
    <!-- ... -->
  </Animals>
  
  <Horses count="152">
    <Ped model="A_C_Horse_Gang_Lenny" name="Horse Gang Lenny"/>
    <!-- ... -->
  </Horses>
  
  <Fish count="56">
    <Ped model="A_C_FishBluegill_01_MS" name="Bluegill" type="regular"/>
    <Ped model="A_C_FishBluegill_01_Legendary" name="Legendary Bluegill" type="legendary"/>
    <!-- ... -->
  </Fish>
  
  <LegendaryAnimals count="21">
    <Ped model="A_C_Bear_01_Legendary" name="Legendary Bharati Grizzly Bear" type="legendary"/>
    <Ped model="A_C_Bison_01_White" name="Legendary White Bison" type="legendary"/>
    <!-- ... -->
  </LegendaryAnimals>
</PedDatabase>
```

### Key Features
- **Complete Coverage**: Includes all existing peds from the codebase
- **Enhanced Data**: Added missing legendary animals and fish species
- **Proper Categorization**: Smart categorization based on model names and patterns
- **Readable Names**: Converted technical model names to user-friendly display names
- **Type Attributes**: Distinguishes between regular and legendary variants
- **Extensible**: Easy to add new peds or categories

## Legendary Animals Included
- Legendary Bharati Grizzly Bear
- Legendary White Bison
- Legendary Tatanka Bison
- Legendary Buck
- Legendary Cougar
- Legendary Elk
- Legendary Fox
- Legendary Moose
- Legendary Panther
- Legendary Wolf
- Legendary Beaver
- Legendary Boar
- Legendary Bighorn Ram
- Legendary Pronghorn
- Legendary Alligator
- Legendary Coyote
- Plus 5 Online legendary variants

## Fish Species Included
### Regular Fish (28 species):
- Bluegill, Bass varieties, Pike, Salmon, Trout, Catfish, etc.

### Legendary Fish (13 species):
- Legendary variants of all major fish species

## Story Characters Included
- Arthur Morgan (Player_Zero)
- John Marston (Player_Three, CS_johnmarston)
- Dutch van der Linde (CS_dutch)
- Micah Bell (CS_micahbell)
- Charles Smith (CS_charlessmith)
- Javier Escuella (CS_javierescuella)
- Bill Williamson (CS_billwilliamson)
- Hosea Matthews (CS_hoseamatthews)
- Abigail Roberts (CS_abigailroberts)
- And many more NPCs and story characters

## Usage for UI Implementation
This database is ready to be integrated into the Spawners > Peds > Ped Database UI:

1. **Parse XML**: Load the database in the application
2. **Category Navigation**: Use the 5 main categories for navigation
3. **Search/Filter**: Filter by name or model
4. **Spawn Integration**: Use model names directly with spawning functions

## Next Steps
1. Integrate database into the Ped Database UI
2. Implement category-based navigation (Humans, Animals, Horses, Fish, Legendary Animals)
3. Add search and filter functionality
4. Connect to existing spawning system
5. Remove temporary database files after integration

## Notes
- Database is comprehensive but may still have some missing variants
- Model names are based on existing codebase and research
- Some legendary animal models may need verification in-game
- Fish models follow the pattern `A_C_Fish[Species]_01_MS` for regular and `A_C_Fish[Species]_01_Legendary` for legendary
- All existing functionality is preserved - this only adds organization and missing entries

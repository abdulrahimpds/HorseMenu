#!/usr/bin/env python3
"""
RDR2 Ped Database Builder
Analyzes existing ped data and creates comprehensive categorized database
"""

import re
import json
import xml.etree.ElementTree as ET
from xml.dom import minidom

# Existing ped data from the codebase (extracted from PedModels.hpp)
existing_peds = [
    "A_C_Cougar_03", "A_C_Horse_Gang_Lenny", "A_C_Horse_Gang_Sadie_EndlessSummer",
    "A_F_M_ARMCHOLERACORPSE_01", "A_F_M_ARMTOWNFOLK_01", "A_F_M_ArmTownfolk_02",
    # ... (truncated for brevity, will add full list)
]

# Legendary animals based on research
legendary_animals = {
    "A_C_Bear_01_Legendary": "Legendary Bharati Grizzly Bear",
    "A_C_Bison_01_Legendary": "Legendary Tatanka Bison", 
    "A_C_Bison_01_White": "Legendary White Bison",
    "A_C_Buck_01_Legendary": "Legendary Buck",
    "A_C_Cougar_01_Legendary": "Legendary Cougar",
    "A_C_Elk_01_Legendary": "Legendary Elk",
    "A_C_Fox_01_Legendary": "Legendary Fox",
    "A_C_Moose_01_Legendary": "Legendary Moose",
    "A_C_Panther_01_Legendary": "Legendary Panther",
    "A_C_Wolf_01_Legendary": "Legendary Wolf",
    "A_C_Beaver_01_Legendary": "Legendary Beaver",
    "A_C_Boar_01_Legendary": "Legendary Boar",
    "A_C_Ram_01_Legendary": "Legendary Bighorn Ram",
    "A_C_Pronghorn_01_Legendary": "Legendary Pronghorn",
    "A_C_Alligator_01_Legendary": "Legendary Alligator",
    "A_C_Coyote_01_Legendary": "Legendary Coyote"
}

# Fish species based on research
fish_species = {
    "A_C_FishBluegill_01_MS": "Bluegill",
    "A_C_FishBullHeadCat_01_MS": "Bullhead Catfish", 
    "A_C_FishChainPickerel_01_MS": "Chain Pickerel",
    "A_C_FishChannelCatfish_01_MS": "Channel Catfish",
    "A_C_FishLakeSturgeon_01_MS": "Lake Sturgeon",
    "A_C_FishLargemouthBass_01_MS": "Largemouth Bass",
    "A_C_FishMuskie_01_MS": "Muskie",
    "A_C_FishNorthernPike_01_MS": "Northern Pike",
    "A_C_FishPerch_01_MS": "Perch",
    "A_C_FishRedfin_01_MS": "Redfin Pickerel",
    "A_C_FishRockBass_01_MS": "Rock Bass",
    "A_C_FishSalmon_01_MS": "Sockeye Salmon",
    "A_C_FishSmallmouthBass_01_MS": "Smallmouth Bass",
    "A_C_FishSteelheadTrout_01_MS": "Steelhead Trout",
    "A_C_FishRainbowTrout_01_MS": "Rainbow Trout"
}

# Legendary fish
legendary_fish = {
    "A_C_FishBluegill_01_Legendary": "Legendary Bluegill",
    "A_C_FishBullHeadCat_01_Legendary": "Legendary Bullhead Catfish",
    "A_C_FishChainPickerel_01_Legendary": "Legendary Chain Pickerel", 
    "A_C_FishChannelCatfish_01_Legendary": "Legendary Channel Catfish",
    "A_C_FishLakeSturgeon_01_Legendary": "Legendary Lake Sturgeon",
    "A_C_FishLargemouthBass_01_Legendary": "Legendary Largemouth Bass",
    "A_C_FishMuskie_01_Legendary": "Legendary Muskie",
    "A_C_FishNorthernPike_01_Legendary": "Legendary Northern Pike",
    "A_C_FishPerch_01_Legendary": "Legendary Perch",
    "A_C_FishRedfin_01_Legendary": "Legendary Redfin Pickerel",
    "A_C_FishRockBass_01_Legendary": "Legendary Rock Bass",
    "A_C_FishSalmon_01_Legendary": "Legendary Sockeye Salmon",
    "A_C_FishSmallmouthBass_01_Legendary": "Legendary Smallmouth Bass"
}

def categorize_ped(ped_name):
    """Categorize a ped based on its name pattern"""
    name_lower = ped_name.lower()
    
    # Legendary animals
    if "legendary" in name_lower or ped_name in legendary_animals:
        return "legendary_animals"
    
    # Fish
    if "fish" in name_lower or ped_name in fish_species or ped_name in legendary_fish:
        return "fish"
    
    # Horses
    if "horse" in name_lower or "_horse_" in name_lower:
        return "horses"
    
    # Animals (A_C_ prefix typically indicates animals)
    if ped_name.startswith("A_C_") or any(animal in name_lower for animal in [
        "bear", "wolf", "cougar", "deer", "elk", "bison", "buffalo", "fox", "rabbit",
        "squirrel", "rat", "bird", "eagle", "hawk", "owl", "duck", "goose", "turkey",
        "chicken", "pig", "cow", "bull", "sheep", "goat", "dog", "cat", "alligator",
        "snake", "turtle", "frog", "beaver", "otter", "skunk", "raccoon", "opossum",
        "bat", "boar", "ram", "pronghorn", "moose", "coyote", "panther", "jaguar"
    ]):
        return "animals"
    
    # Humans (everything else is likely human)
    return "humans"

def create_xml_database():
    """Create comprehensive XML database"""
    root = ET.Element("PedDatabase")
    root.set("version", "1.0")
    root.set("game", "RDR2")
    
    # Create categories
    categories = {
        "humans": ET.SubElement(root, "Humans"),
        "animals": ET.SubElement(root, "Animals"), 
        "horses": ET.SubElement(root, "Horses"),
        "fish": ET.SubElement(root, "Fish"),
        "legendary_animals": ET.SubElement(root, "LegendaryAnimals")
    }
    
    # Add legendary animals first
    for model, name in legendary_animals.items():
        ped_elem = ET.SubElement(categories["legendary_animals"], "Ped")
        ped_elem.set("model", model)
        ped_elem.set("name", name)
        ped_elem.set("type", "legendary")
    
    # Add fish
    for model, name in fish_species.items():
        ped_elem = ET.SubElement(categories["fish"], "Ped")
        ped_elem.set("model", model)
        ped_elem.set("name", name)
        ped_elem.set("type", "regular")
    
    # Add legendary fish
    for model, name in legendary_fish.items():
        ped_elem = ET.SubElement(categories["fish"], "Ped")
        ped_elem.set("model", model)
        ped_elem.set("name", name)
        ped_elem.set("type", "legendary")
    
    return root

if __name__ == "__main__":
    # Create the database
    xml_root = create_xml_database()
    
    # Pretty print XML
    rough_string = ET.tostring(xml_root, 'unicode')
    reparsed = minidom.parseString(rough_string)
    pretty_xml = reparsed.toprettyxml(indent="  ")
    
    # Save to file
    with open("rdr2_ped_database.xml", "w", encoding="utf-8") as f:
        f.write(pretty_xml)
    
    print("Created rdr2_ped_database.xml")
    print("Next: Run the script to analyze existing peds and add them to categories")

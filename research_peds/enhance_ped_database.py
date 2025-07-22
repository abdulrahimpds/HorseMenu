#!/usr/bin/env python3
"""
Enhance the ped database with missing legendary animals, fish, and other creatures
"""

import xml.etree.ElementTree as ET
from xml.dom import minidom

def add_missing_legendary_animals():
    """Add legendary animals that are missing from the database"""
    return {
        # Legendary Animals (based on RDR2 research)
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
        "A_C_Coyote_01_Legendary": "Legendary Coyote",
        
        # Online Legendary Animals
        "MP_A_C_Alligator_01": "Legendary Sun Gator",
        "MP_A_C_Bear_01": "Legendary Ridgeback Spirit Bear", 
        "MP_A_C_Beaver_01": "Legendary Knight Moose",
        "MP_A_C_Boar_01": "Legendary Wakpa Boar",
        "MP_A_C_Buck_01": "Legendary Onyx Wolf",
        "MP_A_C_Cougar_01": "Legendary Shadow Buck",
        "MP_A_C_Coyote_01": "Legendary Emerald Wolf",
        "MP_A_C_Elk_01": "Legendary Marble Fox",
        "MP_A_C_Fox_01": "Legendary Cross Fox",
        "MP_A_C_Moose_01": "Legendary Midnight Paw Coyote",
        "MP_A_C_Panther_01": "Legendary Ghost Panther",
        "MP_A_C_Wolf_01": "Legendary Moonstone Wolf"
    }

def add_missing_fish():
    """Add fish species including legendary variants"""
    return {
        # Regular Fish
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
        "A_C_FishRainbowTrout_01_MS": "Rainbow Trout",
        
        # Legendary Fish
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

def add_missing_animals():
    """Add common animals that might be missing"""
    return {
        # Common animals that should exist
        "A_C_Deer_01": "White-tailed Deer",
        "A_C_Bear_01": "American Black Bear",
        "A_C_Wolf_01": "Gray Wolf", 
        "A_C_Cougar_01": "Cougar",
        "A_C_Elk_01": "American Elk",
        "A_C_Bison_01": "American Bison",
        "A_C_Fox_01": "Red Fox",
        "A_C_Rabbit_01": "Cottontail Rabbit",
        "A_C_Squirrel_01": "Fox Squirrel",
        "A_C_Rat_01": "Brown Rat",
        "A_C_Beaver_01": "North American Beaver",
        "A_C_Boar_01": "Wild Boar",
        "A_C_Ram_01": "Bighorn Sheep",
        "A_C_Pronghorn_01": "Pronghorn Antelope",
        "A_C_Alligator_01": "American Alligator",
        "A_C_Coyote_01": "Coyote",
        "A_C_Panther_01": "Florida Panther",
        "A_C_Moose_01": "Moose",
        "A_C_Duck_01": "Mallard Duck",
        "A_C_Eagle_01": "Bald Eagle",
        "A_C_Hawk_01": "Red-tailed Hawk",
        "A_C_Owl_01": "Great Horned Owl",
        "A_C_Turkey_01": "Wild Turkey",
        "A_C_Chicken_01": "Chicken",
        "A_C_Pig_01": "Domestic Pig",
        "A_C_Cow_01": "Cattle",
        "A_C_Bull_01": "Bull",
        "A_C_Sheep_01": "Domestic Sheep",
        "A_C_Goat_01": "Goat",
        "A_C_Dog_01": "Domestic Dog",
        "A_C_Cat_01": "Domestic Cat"
    }

def enhance_database():
    """Enhance the existing database with missing creatures"""
    
    # Load existing database
    try:
        tree = ET.parse("rdr2_comprehensive_ped_database.xml")
        root = tree.getroot()
    except FileNotFoundError:
        print("Creating new database...")
        root = ET.Element("PedDatabase")
        root.set("version", "1.0")
        root.set("game", "RDR2")
    
    # Find or create categories
    categories = {}
    for category_name in ["Humans", "Animals", "Horses", "Fish", "LegendaryAnimals"]:
        category_elem = root.find(category_name)
        if category_elem is None:
            category_elem = ET.SubElement(root, category_name)
        categories[category_name.lower()] = category_elem
    
    # Get existing models to avoid duplicates
    existing_models = set()
    for ped in root.iter("Ped"):
        model = ped.get("model")
        if model:
            existing_models.add(model)
    
    # Add missing legendary animals
    legendary_animals = add_missing_legendary_animals()
    added_legendary = 0
    for model, name in legendary_animals.items():
        if model not in existing_models:
            ped_elem = ET.SubElement(categories["legendaryanimals"], "Ped")
            ped_elem.set("model", model)
            ped_elem.set("name", name)
            ped_elem.set("type", "legendary")
            added_legendary += 1
    
    # Add missing fish
    fish_species = add_missing_fish()
    added_fish = 0
    for model, name in fish_species.items():
        if model not in existing_models:
            ped_elem = ET.SubElement(categories["fish"], "Ped")
            ped_elem.set("model", model)
            ped_elem.set("name", name)
            if "legendary" in name.lower():
                ped_elem.set("type", "legendary")
            else:
                ped_elem.set("type", "regular")
            added_fish += 1
    
    # Add missing animals
    animals = add_missing_animals()
    added_animals = 0
    for model, name in animals.items():
        if model not in existing_models:
            ped_elem = ET.SubElement(categories["animals"], "Ped")
            ped_elem.set("model", model)
            ped_elem.set("name", name)
            ped_elem.set("type", "regular")
            added_animals += 1
    
    # Update counts
    for category_name, category_elem in categories.items():
        count = len(category_elem.findall("Ped"))
        category_elem.set("count", str(count))
    
    total_peds = len(root.findall(".//Ped"))
    root.set("total_peds", str(total_peds))
    
    return root, added_legendary, added_fish, added_animals, total_peds

if __name__ == "__main__":
    xml_root, legendary_added, fish_added, animals_added, total = enhance_database()
    
    # Pretty print XML
    rough_string = ET.tostring(xml_root, 'unicode')
    reparsed = minidom.parseString(rough_string)
    pretty_xml = reparsed.toprettyxml(indent="  ")
    
    # Save enhanced database
    with open("rdr2_enhanced_ped_database.xml", "w", encoding="utf-8") as f:
        f.write(pretty_xml)
    
    print("Enhanced database created: rdr2_enhanced_ped_database.xml")
    print(f"\nAdded:")
    print(f"  Legendary Animals: {legendary_added}")
    print(f"  Fish: {fish_added}")
    print(f"  Regular Animals: {animals_added}")
    print(f"\nTotal peds in database: {total}")
    
    print("\nDatabase is ready for UI integration!")
    print("Categories: Humans, Animals, Horses, Fish, Legendary Animals")

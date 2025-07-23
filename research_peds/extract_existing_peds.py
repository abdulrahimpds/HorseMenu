#!/usr/bin/env python3
"""
Extract all existing peds from PedModels.hpp and categorize them
"""

import re
import xml.etree.ElementTree as ET
from xml.dom import minidom

def extract_peds_from_hpp():
    """Extract ped names from the existing PedModels.hpp file"""
    peds = []
    
    try:
        with open("src/game/rdr/data/PedModels.hpp", "r", encoding="utf-8") as f:
            content = f.read()
            
        # Find all ped model entries using regex
        # Pattern: {"model_name"_J, "model_name"}
        pattern = r'\{"([^"]+)"_J,\s*"([^"]+)"\}'
        matches = re.findall(pattern, content)
        
        for match in matches:
            peds.append(match[1])  # Use the second part (display name)
            
    except FileNotFoundError:
        print("PedModels.hpp not found, using fallback list")
        # Fallback with some known peds
        peds = [
            "A_C_Cougar_03", "A_C_Horse_Gang_Lenny", "CS_abigailroberts",
            "CS_dutch", "CS_johnmarston", "Player_Zero", "Player_Three"
        ]
    
    return peds

def categorize_ped(ped_name):
    """Categorize a ped based on its name pattern"""
    name_lower = ped_name.lower()
    
    # Story characters and named NPCs (CS_ prefix)
    if ped_name.startswith("CS_"):
        return "humans"
    
    # Player models
    if "player_" in name_lower:
        return "humans"
    
    # Horses
    if "horse" in name_lower or "_horse_" in name_lower:
        return "horses"
    
    # Fish (will be handled separately with legendary variants)
    if "fish" in name_lower:
        return "fish"
    
    # Animals (A_C_ prefix typically indicates animals, but exclude horses and fish)
    if ped_name.startswith("A_C_") and "horse" not in name_lower and "fish" not in name_lower:
        return "animals"
    
    # Everything else is human
    return "humans"

def get_display_name(ped_model):
    """Generate a readable display name from model name"""
    # Remove prefixes and clean up
    name = ped_model
    
    # Handle CS_ (cutscene/story) characters
    if name.startswith("CS_"):
        name = name[3:]  # Remove CS_ prefix
        
    # Handle A_C_ (animal) models
    if name.startswith("A_C_"):
        name = name[4:]  # Remove A_C_ prefix
        
    # Handle other prefixes
    prefixes_to_remove = ["A_F_M_", "A_M_M_", "A_M_O_", "A_F_O_", "G_M_M_", "S_M_M_"]
    for prefix in prefixes_to_remove:
        if name.startswith(prefix):
            name = name[len(prefix):]
            break
    
    # Replace underscores with spaces and title case
    name = name.replace("_", " ").title()
    
    # Handle special cases
    special_names = {
        "Abigailroberts": "Abigail Roberts",
        "Dutch": "Dutch van der Linde", 
        "Johnmarston": "John Marston",
        "Player Zero": "Arthur Morgan",
        "Player Three": "John Marston",
        "Micahbell": "Micah Bell",
        "Charlessmith": "Charles Smith",
        "Javierescuella": "Javier Escuella",
        "Hoseamatthews": "Hosea Matthews",
        "Billwilliamson": "Bill Williamson"
    }
    
    for key, value in special_names.items():
        if key.lower() in name.lower():
            return value
    
    return name

def create_comprehensive_database():
    """Create comprehensive XML database with all existing peds"""
    
    # Extract existing peds
    existing_peds = extract_peds_from_hpp()
    print(f"Found {len(existing_peds)} existing peds")
    
    # Create XML structure
    root = ET.Element("PedDatabase")
    root.set("version", "1.0")
    root.set("game", "RDR2")
    root.set("total_peds", str(len(existing_peds)))
    
    # Create categories
    categories = {
        "humans": ET.SubElement(root, "Humans"),
        "animals": ET.SubElement(root, "Animals"), 
        "horses": ET.SubElement(root, "Horses"),
        "fish": ET.SubElement(root, "Fish"),
        "legendary_animals": ET.SubElement(root, "LegendaryAnimals")
    }
    
    # Add description to each category
    categories["humans"].set("description", "Human NPCs, story characters, and player models")
    categories["animals"].set("description", "Wildlife and domestic animals")
    categories["horses"].set("description", "Horse breeds and variants")
    categories["fish"].set("description", "Fish species for fishing")
    categories["legendary_animals"].set("description", "Rare legendary animal variants")
    
    # Categorize and add all peds
    category_counts = {"humans": 0, "animals": 0, "horses": 0, "fish": 0, "legendary_animals": 0}
    
    for ped_model in sorted(existing_peds):
        category = categorize_ped(ped_model)
        display_name = get_display_name(ped_model)
        
        ped_elem = ET.SubElement(categories[category], "Ped")
        ped_elem.set("model", ped_model)
        ped_elem.set("name", display_name)
        
        # Add special attributes
        if category == "animals" and any(x in ped_model.lower() for x in ["legendary", "rare", "unique"]):
            ped_elem.set("rarity", "legendary")
            
        category_counts[category] += 1
    
    # Add counts to categories
    for cat_name, count in category_counts.items():
        categories[cat_name].set("count", str(count))
    
    return root, category_counts

if __name__ == "__main__":
    xml_root, counts = create_comprehensive_database()
    
    # Pretty print XML
    rough_string = ET.tostring(xml_root, 'unicode')
    reparsed = minidom.parseString(rough_string)
    pretty_xml = reparsed.toprettyxml(indent="  ")
    
    # Save to file
    with open("rdr2_comprehensive_ped_database.xml", "w", encoding="utf-8") as f:
        f.write(pretty_xml)
    
    print("Created rdr2_comprehensive_ped_database.xml")
    print("\nCategory breakdown:")
    for category, count in counts.items():
        print(f"  {category.title()}: {count} peds")
    
    print(f"\nTotal: {sum(counts.values())} peds")
    print("\nDatabase ready for integration into the UI!")

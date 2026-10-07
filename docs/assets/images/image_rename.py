import os
import re
from pathlib import Path
from PIL import Image
from datetime import datetime

def is_valid_filename(filename):
    """
    Check if filename follows the naming guidelines:
    - lowercase only
    - underscores between words
    - no special characters (only letters, numbers, and underscores)
    """
    pattern = r'^[a-z0-9_]+$'
    return bool(re.match(pattern, filename))

def show_image_with_pil(image_path):
    """
    Display image using PIL's built-in viewer.
    """
    try:
        img = Image.open(image_path)
        # Get image info
        print(f"   📐 Size: {img.size[0]}x{img.size[1]} pixels")
        print(f"   🎨 Mode: {img.mode}")
        print(f"   📄 Format: {img.format}")
        img.show()
        return True
    except Exception as e:
        print(f"❌ Error opening image: {e}")
        return False

def get_valid_input(current_name):
    """
    Prompt user for valid filename input.
    """
    while True:
        new_name = input(f"\nEnter new name for '{current_name}' (without extension): ").strip()
        
        if not new_name:
            print("❌ Filename cannot be empty!")
            continue
        
        if not is_valid_filename(new_name):
            print("❌ Invalid filename!")
            print("   Rules: lowercase only, use underscores between words, no special characters")
            print("   Example: my_image_file")
            continue
        
        return new_name

def generate_report(report_data, folder_path):
    """
    Generate a text report of all renamed files.
    """
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_filename = f"rename_report_{timestamp}.txt"
    report_path = Path(folder_path) / report_filename
    
    try:
        with open(report_path, 'w', encoding='utf-8') as f:
            f.write("=" * 80 + "\n")
            f.write("IMAGE RENAMING REPORT\n")
            f.write("=" * 80 + "\n")
            f.write(f"Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"Folder: {Path(folder_path).absolute()}\n")
            f.write("=" * 80 + "\n\n")
            
            # Renamed files section
            if report_data['renamed']:
                f.write(f"RENAMED FILES ({len(report_data['renamed'])})\n")
                f.write("-" * 80 + "\n")
                for item in report_data['renamed']:
                    f.write(f"  OLD: {item['old']}\n")
                    f.write(f"  NEW: {item['new']}\n")
                    f.write("\n")
            else:
                f.write("RENAMED FILES (0)\n")
                f.write("-" * 80 + "\n")
                f.write("  No files were renamed.\n\n")
            
            # Skipped files section
            if report_data['skipped']:
                f.write(f"\nSKIPPED FILES ({len(report_data['skipped'])})\n")
                f.write("-" * 80 + "\n")
                for item in report_data['skipped']:
                    f.write(f"  • {item}\n")
                f.write("\n")
            
            # Failed files section
            if report_data['failed']:
                f.write(f"\nFAILED RENAMES ({len(report_data['failed'])})\n")
                f.write("-" * 80 + "\n")
                for item in report_data['failed']:
                    f.write(f"  File: {item['file']}\n")
                    f.write(f"  Error: {item['error']}\n")
                    f.write("\n")
            
            # Summary section
            f.write("\n" + "=" * 80 + "\n")
            f.write("SUMMARY\n")
            f.write("=" * 80 + "\n")
            f.write(f"Total files processed: {report_data['total']}\n")
            f.write(f"Successfully renamed: {len(report_data['renamed'])}\n")
            f.write(f"Skipped: {len(report_data['skipped'])}\n")
            f.write(f"Failed: {len(report_data['failed'])}\n")
            f.write("=" * 80 + "\n")
        
        return report_path
    except Exception as e:
        print(f"❌ Error generating report: {e}")
        return None

def generate_csv_report(report_data, folder_path):
    """
    Generate a CSV report of renamed files.
    """
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_filename = f"rename_report_{timestamp}.csv"
    csv_path = Path(folder_path) / csv_filename
    
    try:
        with open(csv_path, 'w', encoding='utf-8') as f:
            # Header
            f.write("Status,Old Filename,New Filename,Error\n")
            
            # Renamed files
            for item in report_data['renamed']:
                f.write(f"Renamed,\"{item['old']}\",\"{item['new']}\",\n")
            
            # Skipped files
            for item in report_data['skipped']:
                f.write(f"Skipped,\"{item}\",,\n")
            
            # Failed files
            for item in report_data['failed']:
                f.write(f"Failed,\"{item['file']}\",,\"{item['error']}\"\n")
        
        return csv_path
    except Exception as e:
        print(f"❌ Error generating CSV report: {e}")
        return None

def print_console_report(report_data):
    """
    Print a summary report to the console.
    """
    print("\n" + "=" * 80)
    print("DETAILED REPORT")
    print("=" * 80)
    
    if report_data['renamed']:
        print(f"\n✅ RENAMED FILES ({len(report_data['renamed'])}):")
        print("-" * 80)
        for item in report_data['renamed']:
            print(f"  {item['old']} → {item['new']}")
    
    if report_data['skipped']:
        print(f"\n⏭️  SKIPPED FILES ({len(report_data['skipped'])}):")
        print("-" * 80)
        for item in report_data['skipped']:
            print(f"  • {item}")
    
    if report_data['failed']:
        print(f"\n❌ FAILED RENAMES ({len(report_data['failed'])}):")
        print("-" * 80)
        for item in report_data['failed']:
            print(f"  • {item['file']}: {item['error']}")
    
    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total files processed: {report_data['total']}")
    print(f"Successfully renamed: {len(report_data['renamed'])}")
    print(f"Skipped: {len(report_data['skipped'])}")
    print(f"Failed: {len(report_data['failed'])}")
    print("=" * 80)

def rename_images(folder_path):
    """
    Process all images in the specified folder.
    """
    # Common image extensions
    image_extensions = {'.jpg', '.jpeg', '.png', '.gif', '.bmp', '.tiff', '.webp', '.ico'}
    
    # Convert to Path object
    folder = Path(folder_path)
    
    if not folder.exists():
        print(f"❌ Error: Folder '{folder_path}' does not exist!")
        return
    
    if not folder.is_dir():
        print(f"❌ Error: '{folder_path}' is not a directory!")
        return
    
    # Get all image files
    image_files = sorted([f for f in folder.iterdir() 
                         if f.is_file() and f.suffix.lower() in image_extensions])
    
    if not image_files:
        print(f"📁 No image files found in '{folder_path}'")
        return
    
    print(f"\n📁 Found {len(image_files)} image(s) in '{folder_path}'")
    print("=" * 60)
    
    # Initialize report data
    report_data = {
        'renamed': [],
        'skipped': [],
        'failed': [],
        'total': len(image_files)
    }
    
    for i, image_file in enumerate(image_files, 1):
        print(f"\n{'='*60}")
        print(f"[{i}/{len(image_files)}] Current file: {image_file.name}")
        print(f"{'='*60}")
        
        # Show the image for viewing
        print("🖼️  Opening image...")
        if not show_image_with_pil(image_file):
            retry = input("Failed to open image. Continue anyway? (y/n): ").strip().lower()
            if retry != 'y':
                report_data['skipped'].append(image_file.name)
                continue
        
        # Wait for user to view the image
        input("\n⏸️  Press Enter after viewing the image...")
        
        # Ask if user wants to rename
        action = input("\nRename this image? (y/n/q to quit): ").strip().lower()
        
        if action == 'q':
            print("\n🛑 Quitting...")
            # Add remaining files to skipped
            for j in range(i, len(image_files)):
                if image_files[j] != image_file:
                    report_data['skipped'].append(image_files[j].name)
            break
        
        if action != 'y':
            print("⏭️  Skipped")
            report_data['skipped'].append(image_file.name)
            continue
        
        # Get valid new filename
        new_name = get_valid_input(image_file.name)
        
        # Construct new file path
        new_file_path = image_file.parent / f"{new_name}{image_file.suffix.lower()}"
        
        # Check if file already exists
        if new_file_path.exists() and new_file_path != image_file:
            overwrite = input(f"⚠️  File '{new_file_path.name}' already exists. Overwrite? (y/n): ").strip().lower()
            if overwrite != 'y':
                print("⏭️  Skipped")
                report_data['skipped'].append(image_file.name)
                continue
        
        # Rename the file
        try:
            old_name = image_file.name
            image_file.rename(new_file_path)
            new_name_full = new_file_path.name
            print(f"✅ Renamed to: {new_name_full}")
            report_data['renamed'].append({
                'old': old_name,
                'new': new_name_full
            })
        except Exception as e:
            print(f"❌ Error renaming file: {e}")
            report_data['failed'].append({
                'file': image_file.name,
                'error': str(e)
            })
    
    # Print console report
    print_console_report(report_data)
    
    # Generate text file report
    report_path = generate_report(report_data, folder_path)
    if report_path:
        print(f"\n📄 Text report saved to: {report_path}")
    
    # Generate CSV report
    csv_path = generate_csv_report(report_data, folder_path)
    if csv_path:
        print(f"📊 CSV report saved to: {csv_path}")
    
    return report_data

def main():
    """
    Main function to run the script.
    """
    print("=" * 60)
    print("🖼️  IMAGE RENAMING TOOL")
    print("=" * 60)
    print("\nNaming Guidelines:")
    print("  • Lowercase letters only")
    print("  • Use underscores between words")
    print("  • No special characters (only a-z, 0-9, _)")
    print("  • Example: my_vacation_photo")
    print("=" * 60)
    print("\nℹ️  Each image will open for you to view")
    print("=" * 60)
    
    # Get folder path from user
    folder_path = input("\nEnter folder path (or press Enter for current directory): ").strip()
    
    if not folder_path:
        folder_path = "."
    
    rename_images(folder_path)
    
    print("\n✨ Done!")

if __name__ == "__main__":
    main()
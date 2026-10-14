#!/usr/bin/env python3
"""Build an OpenYAMM table overlay from the MAW mod's data tables.

MAW ships changed copies of Might and Magic Merge tables. OpenYAMM's tables descend from the same Merge tables but
some have changes of their own, so MAW's files cannot simply replace them. For every table this tool takes

    base  the Merge table MAW started from
    maw   MAW's version
    ours  OpenYAMM's version

and writes  ours + (maw - base): MAW's cell changes applied on top of OpenYAMM's table, row by row, keyed by the id
in the first column. Where OpenYAMM never changed a table the result is simply MAW's table. A cell changed by both
sides to different values is a conflict: MAW's value wins and the conflict is listed in the report.

The output folder is meant to be mounted over the engine assets (mods.yml: `assets:`), so files land at the same
relative paths OpenYAMM loads them from.

Example:
    python tools/maw/convert_tables.py \\
        --maw C:/path/Maw-Mod-MMMerge \\
        --merge-install C:/path/to/merge/game \\
        --merge-base C:/path/mmmerge-checkout \\
        --out mods/maw/assets
"""
import argparse
import os
import struct
import sys
import zlib

ENCODING = 'latin1'


# ---------------------------------------------------------------------------------------------------------------------
# sources

def read_lod_text_files(lod_path):
    """Returns {lower-case name: bytes} for an MM8-format text archive ("MMVIII" LOD with packed entries)."""
    files = {}

    with open(lod_path, 'rb') as stream:
        head = stream.read(0x120)

        if head[:4] != b'LOD\0':
            raise ValueError(f'{lod_path} is not a LOD archive')

        version = head[4:0x54].split(b'\0')[0]
        offset, _size, _unknown, count = struct.unpack_from('<IIIH', head, 0x110)
        entry_size = 0x4C if version.startswith(b'MMVIII') else 0x20
        name_size = 0x40 if entry_size == 0x4C else 0x10
        stream.seek(offset)
        table = stream.read(entry_size * count)

        for index in range(count):
            entry = table[index * entry_size:(index + 1) * entry_size]
            name = entry[:name_size].split(b'\0')[0].decode(ENCODING)
            entry_offset, entry_length = struct.unpack_from('<II', entry, name_size)
            stream.seek(offset + entry_offset)
            files[name.lower()] = unpack_lod_entry(stream.read(entry_length))

    return files


def unpack_lod_entry(raw):
    # name[0x40], u32 0, packed size, ..., unpacked size at 0x58, data at 0x60
    if len(raw) >= 0x60:
        packed = struct.unpack_from('<I', raw, 0x44)[0]
        unpacked = struct.unpack_from('<I', raw, 0x58)[0]

        if packed == len(raw) - 0x60:
            body = raw[0x60:]

            if unpacked == 0:
                return body

            data = zlib.decompress(body)

            if len(data) == unpacked:
                return data

    return raw


def read_folder_file(folder, name):
    if folder is None or not os.path.isdir(folder):
        return None

    for entry in os.listdir(folder):
        if entry.lower() == name.lower():
            with open(os.path.join(folder, entry), 'rb') as stream:
                return stream.read()

    return None


# ---------------------------------------------------------------------------------------------------------------------
# tables

def parse_table(data):
    if data is None:
        return None

    text = data.decode(ENCODING).replace('\r\n', '\n').replace('\r', '\n')
    rows = [line.split('\t') for line in text.split('\n')]

    while rows and rows[-1] == ['']:
        rows.pop()

    return rows


def serialize_table(rows):
    return ('\r\n'.join('\t'.join(row) for row in rows) + '\r\n').encode(ENCODING)


def cell(row, index):
    return row[index] if index < len(row) else ''


def normalized(row):
    cells = [value.strip() for value in row]

    while cells and cells[-1] == '':
        cells.pop()

    return cells


def tables_equal(left, right):
    if len(left) != len(right):
        # trailing blank rows do not count
        left = [row for row in left if normalized(row)]
        right = [row for row in right if normalized(row)]

    return len(left) == len(right) and all(normalized(a) == normalized(b) for a, b in zip(left, right))


def row_key(row, key_column):
    value = cell(row, key_column).strip()
    return int(value) if value.lstrip('-').isdigit() else None


def index_rows(rows, key_column):
    """Returns ({key: row index}, duplicate keys). Rows without an integer key are structural (headers, blanks)."""
    index = {}
    duplicates = set()

    for position, row in enumerate(rows):
        key = row_key(row, key_column)

        if key is None:
            continue

        if key in index:
            duplicates.add(key)
        else:
            index[key] = position

    return index, duplicates


class TableResult:
    def __init__(self, name):
        self.name = name
        self.mode = ''
        self.rows = None
        self.cells_applied = 0
        self.rows_added = 0
        self.conflicts = []
        self.notes = []


def merge_table(name, base, maw, ours, key_column=0):
    result = TableResult(name)

    if maw is None:
        result.mode = 'skipped: MAW does not ship this table'
        return result

    if ours is None:
        result.mode = 'skipped: OpenYAMM has no such table'
        return result

    if base is None:
        result.mode = 'skipped: Merge base table not found, cannot tell what MAW changed'
        return result

    if tables_equal(base, maw):
        result.mode = 'unchanged: MAW did not change this table'
        return result

    if tables_equal(base, ours):
        result.mode = 'replaced: OpenYAMM table equals the Merge base, MAW table used as is'
        result.rows = maw
        return result

    base_index, base_duplicates = index_rows(base, key_column)
    maw_index, maw_duplicates = index_rows(maw, key_column)
    ours_index, ours_duplicates = index_rows(ours, key_column)

    if base_duplicates or maw_duplicates or ours_duplicates:
        result.mode = 'skipped: ids in the first column are not unique, needs a table-specific merge'
        result.notes.append(
            f'duplicate ids: base {sorted(base_duplicates)[:5]}, maw {sorted(maw_duplicates)[:5]}, '
            f'ours {sorted(ours_duplicates)[:5]}')
        return result

    merged = [list(row) for row in ours]

    for key, maw_position in maw_index.items():
        maw_row = maw[maw_position]

        if key not in base_index:
            if key in ours_index:
                if normalized(merged[ours_index[key]]) != normalized(maw_row):
                    result.conflicts.append(f'id {key}: added by both MAW and OpenYAMM with different contents; '
                                            f'OpenYAMM row kept')
            else:
                merged.append(list(maw_row))
                result.rows_added += 1

            continue

        base_row = base[base_index[key]]

        if key not in ours_index:
            if normalized(base_row) != normalized(maw_row):
                result.notes.append(f'id {key}: changed by MAW but absent from OpenYAMM, change dropped')

            continue

        target = merged[ours_index[key]]

        for column in range(max(len(base_row), len(maw_row))):
            base_value = cell(base_row, column)
            maw_value = cell(maw_row, column)

            if maw_value.strip() == base_value.strip():
                continue

            ours_value = cell(target, column)

            if ours_value.strip() != base_value.strip() and ours_value.strip() != maw_value.strip():
                result.conflicts.append(
                    f'id {key} column {column}: base {base_value!r}, MAW {maw_value!r}, OpenYAMM {ours_value!r}; '
                    f'MAW value used')

            while len(target) <= column:
                target.append('')

            if target[column] != maw_value:
                target[column] = maw_value
                result.cells_applied += 1

    removed = [key for key in base_index if key not in maw_index]

    if removed:
        result.notes.append(f'{len(removed)} ids removed by MAW were kept (first: {sorted(removed)[:5]})')

    result.mode = 'merged: MAW changes applied to the OpenYAMM table'
    result.rows = merged
    return result


# ---------------------------------------------------------------------------------------------------------------------
# grid tables: a header row of column names and a first column of row names (class x skill tables)

def name_key(value):
    return ''.join(character for character in value.strip().lower() if character.isalnum() or character == '/')


def unique_keys(names):
    """Normalised names with '#2', '#3' appended to repeats, so 'n/u' columns stay distinguishable."""
    seen = {}
    keys = []

    for name in names:
        key = name_key(name)

        if key == '':
            keys.append(None)
            continue

        seen[key] = seen.get(key, 0) + 1
        keys.append(key if seen[key] == 1 else f'{key}#{seen[key]}')

    return keys


def transpose(rows):
    width = max(len(row) for row in rows)
    padded = [row + [''] * (width - len(row)) for row in rows]
    return [[padded[r][c] for r in range(len(padded))] for c in range(width)]


def merge_grid(name, base, maw, ours, row_alias=None, column_alias=None, add_columns=False, add_rows=False,
               fill='-', translate=None):
    """ours + (maw - base) for a table addressed by row name and column name.

    row_alias / column_alias map a base-side key to the list of OpenYAMM keys it corresponds to.
    add_columns / add_rows append what MAW added; otherwise such cells are counted and left out.
    translate(column_key, value) converts a MAW cell into OpenYAMM's vocabulary.
    """
    result = TableResult(name)

    if maw is None or ours is None or base is None:
        result.mode = 'skipped: a source table is missing'
        return result

    row_alias = row_alias or {}
    column_alias = column_alias or {}
    translate = translate or (lambda _column, value: value)
    merged = [list(row) for row in ours]

    base_columns = unique_keys(base[0])
    maw_columns = unique_keys(maw[0])
    ours_columns = unique_keys(merged[0])
    base_rows = unique_keys([cell(row, 0) for row in base])
    maw_rows = unique_keys([cell(row, 0) for row in maw])
    ours_rows = unique_keys([cell(row, 0) for row in merged])

    base_column_at = {key: index for index, key in enumerate(base_columns) if key and index > 0}
    base_row_at = {key: index for index, key in enumerate(base_rows) if key and index > 0}
    ours_column_at = {key: index for index, key in enumerate(ours_columns) if key and index > 0}
    ours_row_at = {key: index for index, key in enumerate(ours_rows) if key and index > 0}
    skipped_new = 0
    dropped = set()

    def ensure_width(row, width):
        while len(row) < width:
            row.append('')

    for maw_row_index, row_key_value in enumerate(maw_rows):
        if maw_row_index == 0 or row_key_value is None:
            continue

        row_is_new = row_key_value not in base_row_at
        target_row_keys = row_alias.get(row_key_value, [row_key_value])

        for maw_column_index, column_key_value in enumerate(maw_columns):
            if maw_column_index == 0 or column_key_value is None:
                continue

            column_is_new = column_key_value not in base_column_at
            maw_value = cell(maw[maw_row_index], maw_column_index).strip()
            base_value = None

            if not row_is_new and not column_is_new:
                base_value = cell(base[base_row_at[row_key_value]], base_column_at[column_key_value]).strip()

                if base_value == maw_value:
                    continue

            if (row_is_new and not add_rows) or (column_is_new and not add_columns):
                skipped_new += 1
                continue

            target_column_keys = column_alias.get(column_key_value, [column_key_value])

            for target_row_key in target_row_keys:
                if target_row_key not in ours_row_at:
                    if not row_is_new:
                        dropped.add(f'row {target_row_key}')
                        continue

                    new_row = [cell(maw[maw_row_index], 0)] + [fill] * (len(merged[0]) - 1)
                    merged.append(new_row)
                    ours_row_at[target_row_key] = len(merged) - 1
                    result.rows_added += 1

                for target_column_key in target_column_keys:
                    if target_column_key not in ours_column_at:
                        if not column_is_new:
                            dropped.add(f'column {target_column_key}')
                            continue

                        merged[0].append(cell(maw[0], maw_column_index))
                        ours_column_at[target_column_key] = len(merged[0]) - 1

                        for other in merged[1:]:
                            if normalized(other):
                                ensure_width(other, len(merged[0]) - 1)
                                other.append(fill)

                    target = merged[ours_row_at[target_row_key]]
                    column = ours_column_at[target_column_key]
                    ensure_width(target, column + 1)
                    new_value = translate(target_column_key, maw_value)
                    ours_value = target[column].strip()

                    if base_value is not None:
                        expected = translate(target_column_key, base_value)

                        if ours_value != expected and ours_value != new_value:
                            result.conflicts.append(
                                f'{cell(maw[maw_row_index], 0)} / {cell(maw[0], maw_column_index)}: base '
                                f'{base_value!r}, MAW {maw_value!r}, OpenYAMM {ours_value!r}; MAW value used')

                    if ours_value != new_value:
                        target[column] = new_value
                        result.cells_applied += 1

    if skipped_new:
        result.notes.append(f'{skipped_new} cells in rows/columns MAW added were left out (new classes come later)')

    if dropped:
        result.notes.append('MAW changed entries OpenYAMM does not have: ' + ', '.join(sorted(dropped)[:8]))

    if result.cells_applied == 0 and result.rows_added == 0:
        result.mode = 'unchanged: nothing to apply'
        return result

    result.mode = 'merged by row and column name: MAW changes applied to the OpenYAMM table'
    result.rows = merged
    return result


def convert_class_skills(base, maw, ours):
    # Same layout on all sides. MAW's twelve extra class columns are appended; the loader ignores names it
    # does not know, so they are inert until the classes exist.
    return merge_grid('Class Skills.txt -> class_skills.txt', base, maw, ours, add_columns=True)


def convert_class_multipliers(base, maw, ours):
    # Same columns under different headers, and the spell point stat spelled out in OpenYAMM.
    if base is None or maw is None or ours is None:
        return merge_grid('Class HP SP.txt -> class_multipliers.txt', base, maw, ours)

    stat_names = {}

    base_by_class = {name_key(cell(row, 0)): row for row in base[1:]}

    for row in ours[1:]:
        base_row = base_by_class.get(name_key(cell(row, 0)))

        if base_row is not None and cell(base_row, 5).strip():
            stat_names.setdefault(cell(base_row, 5).strip(), cell(row, 5).strip())

    stat_column = name_key(cell(ours[0], 5))

    def translate(column_key, value):
        if column_key == stat_column:
            return stat_names.get(value, value)

        return value

    def with_our_header(rows):
        return [list(ours[0][:len(rows[0])]) + rows[0][len(ours[0]):]] + [list(row) for row in rows[1:]]

    return merge_grid('Class HP SP.txt -> class_multipliers.txt', with_our_header(base), with_our_header(maw), ours,
                      add_rows=True, fill='', translate=translate)


STARTING_SKILL_NAMES = {
    'leather': ['leatherarmor'], 'chain': ['chainarmor'], 'plate': ['platearmor'],
    'fire': ['firemagic'], 'air': ['airmagic'], 'water': ['watermagic'], 'earth': ['earthmagic'],
    'spirit': ['spiritmagic'], 'mind': ['mindmagic'], 'body': ['bodymagic'],
    'light': ['lightmagic'], 'dark': ['darkmagic'], 'repair': ['repairitem'],
}


def convert_class_starting_skills(base, maw, ours):
    # Merge stores skills as rows and classes as columns; OpenYAMM stores the transpose and splits the shared
    # "High Priest / Master Wizard" column in two.
    if base is None or maw is None or ours is None:
        return merge_grid('Class Starting Skills.txt -> class_starting_skills.txt', base, maw, ours)

    row_alias = {}

    for header in base[0][1:]:
        if '/' in header and name_key(header) != 'n/u':
            row_alias[name_key(header)] = [name_key(part) for part in header.split('/')]

    result = merge_grid('Class Starting Skills.txt -> class_starting_skills.txt', transpose(base), transpose(maw),
                        ours, row_alias=row_alias, column_alias=STARTING_SKILL_NAMES)

    if result.rows is not None:
        add_new_starting_classes(result, maw)

    return result


# MAW's own starting classes, by the class number the starting-skill column is found by. The Merge keeps starting
# skills by pairs of classes (column = class number // 2), and MAW's labels for its new pairs are stale ("Death
# Knight" twice, "Druid", "Sorcerer"), so the columns are read by position and given the classes' own names.
NEW_STARTING_CLASSES = {'Seraphim': 53, 'Death Knight': 56, 'Shaman': 59, 'Elementalist': 62}


def add_new_starting_classes(result, maw):
    ours_columns = unique_keys(result.rows[0])
    existing = {name_key(cell(row, 0)) for row in result.rows}

    for class_name, class_id in NEW_STARTING_CLASSES.items():
        if name_key(class_name) in existing:
            continue

        maw_column = class_id // 2 + 1
        values = {}

        for maw_row in maw[1:]:
            skill = name_key(cell(maw_row, 0))

            for key in STARTING_SKILL_NAMES.get(skill, [skill]):
                values[key] = cell(maw_row, maw_column).strip() or '-'

        row = [class_name] + [values.get(key, '-') if key else '' for key in ours_columns[1:]]
        result.rows.append(row)
        result.rows_added += 1

    result.notes.append(f'starting classes MAW added, read by class number: {", ".join(NEW_STARTING_CLASSES)}')


# MAW archive table (zMaw.T.lod / mmmerge.T.lod name) -> OpenYAMM path under data_tables
ARCHIVE_TABLES = [
    ('items.txt', 'items.txt'),
    ('monsters.txt', 'monster_data.txt'),
    ('mapstats.txt', 'map_stats.txt'),
    ('placemon.txt', 'english/place_mon.txt'),
    ('potion.txt', 'english/potion.txt'),
    ('potnotes.txt', 'english/potnotes.txt'),
    ('rnditems.txt', 'random_items.txt'),
    ('spcitems.txt', 'special_item_enchants.txt'),
    ('class.txt', 'english/class.txt'),
]

# MAW Data/Tables file -> OpenYAMM path under data_tables, for tables that kept the Merge layout
LOOSE_TABLES = [
    ('Class Extra.txt', 'class_extra.txt'),
    ('Class Starting Stats.txt', 'class_starting_stats.txt'),
    ('Potion settings.txt', 'potion_settings.txt'),
    ('House rules.txt', 'house_rules.txt'),
    ('Complex item pictures.txt', 'complex_item_pictures.txt'),
]

# MAW Data/Tables file -> (OpenYAMM path, converter) for class tables OpenYAMM lays out differently
GRID_TABLES = [
    ('Class Skills.txt', 'class_skills.txt', convert_class_skills),
    ('Class HP SP.txt', 'class_multipliers.txt', convert_class_multipliers),
    ('Class Starting Skills.txt', 'class_starting_skills.txt', convert_class_starting_skills),
]

# MAW tables whose OpenYAMM counterpart has a different layout (or none); not converted yet.
UNCONVERTED = [
    ('Character portraits.txt', 'character_data.txt: OpenYAMM restructured the columns'),
    ('ObjList.txt', 'object_list.txt: OpenYAMM restructured the columns; MAW adds 44 objects'),
    ('Race Skills.txt', 'race_skills.yml: OpenYAMM uses YAML'),
    ('SFT.txt', 'sprite frame tables: OpenYAMM uses YAML'),
    ('Spells2.txt', 'no direct counterpart'),
    ('Class Skillz.txt', 'MAW extended skills, no OpenYAMM counterpart yet'),
    ('Class Starting Skillz.txt', 'MAW extended skills, no OpenYAMM counterpart yet'),
    ('Race Skillz.txt', 'MAW extended skills, no OpenYAMM counterpart yet'),
    ('Extra Skillz.txt', 'MAW extended skills, no OpenYAMM counterpart yet'),
]


def main():
    parser = argparse.ArgumentParser(description='Build an OpenYAMM table overlay from MAW data tables.')
    parser.add_argument('--maw', required=True, help='MAW checkout (contains Data/ and Scripts/)')
    parser.add_argument('--merge-install', required=True,
                        help='folder of a Merge game install holding Data/mmmerge.T.lod (the base MAW started from)')
    parser.add_argument('--merge-base', required=True, help='Merge checkout holding Data/Tables (base of loose tables)')
    parser.add_argument('--openyamm-data', default=None,
                        help='OpenYAMM data_tables folder (default: assets_dev/engine/data_tables next to this tool)')
    parser.add_argument('--out', required=True, help='overlay folder to write, e.g. mods/maw/assets')
    arguments = parser.parse_args()

    repository_root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    ours_folder = arguments.openyamm_data or os.path.join(repository_root, 'assets_dev', 'engine', 'data_tables')
    maw_archive = read_lod_text_files(os.path.join(arguments.maw, 'Data', 'zMaw.T.lod'))
    base_archive = read_lod_text_files(os.path.join(arguments.merge_install, 'Data', 'mmmerge.T.lod'))
    maw_tables_folder = os.path.join(arguments.maw, 'Data', 'Tables')
    base_tables_folder = os.path.join(arguments.merge_base, 'Data', 'Tables')
    # Same layout as the development asset root, which is what AssetFileSystem::addOverlayRoot expects.
    out_folder = os.path.join(arguments.out, 'engine', 'data_tables')

    def read_ours(relative_path):
        path = os.path.join(ours_folder, relative_path)

        if not os.path.isfile(path):
            return None

        with open(path, 'rb') as stream:
            return stream.read()

    results = []

    for archive_name, ours_path in ARCHIVE_TABLES:
        result = merge_table(
            f'{archive_name} -> {ours_path}',
            parse_table(base_archive.get(archive_name)),
            parse_table(maw_archive.get(archive_name)),
            parse_table(read_ours(ours_path)))
        result.output = ours_path
        results.append(result)

    for maw_name, ours_path in LOOSE_TABLES:
        result = merge_table(
            f'{maw_name} -> {ours_path}',
            parse_table(read_folder_file(base_tables_folder, maw_name)),
            parse_table(read_folder_file(maw_tables_folder, maw_name)),
            parse_table(read_ours(ours_path)))
        result.output = ours_path
        results.append(result)

    for maw_name, ours_path, convert in GRID_TABLES:
        result = convert(
            parse_table(read_folder_file(base_tables_folder, maw_name)),
            parse_table(read_folder_file(maw_tables_folder, maw_name)),
            parse_table(read_ours(ours_path)))
        result.output = ours_path
        results.append(result)

    report = ['MAW table conversion report', '']
    written = 0
    conflict_total = 0

    for result in results:
        report.append(result.name)
        report.append(f'    {result.mode}')

        if result.rows is not None:
            target = os.path.join(out_folder, result.output)
            os.makedirs(os.path.dirname(target), exist_ok=True)

            with open(target, 'wb') as stream:
                stream.write(serialize_table(result.rows))

            written += 1
            report.append(f'    rows {len(result.rows)}, cells changed {result.cells_applied}, '
                          f'rows added {result.rows_added}, conflicts {len(result.conflicts)}')

        for note in result.notes:
            report.append(f'    note: {note}')

        for conflict in result.conflicts:
            report.append(f'    CONFLICT: {conflict}')

        conflict_total += len(result.conflicts)
        report.append('')

    report.append('Not converted by this tool:')

    for maw_name, reason in UNCONVERTED:
        if read_folder_file(maw_tables_folder, maw_name) is not None:
            report.append(f'    {maw_name}: {reason}')

    os.makedirs(arguments.out, exist_ok=True)
    report_path = os.path.join(arguments.out, 'conversion_report.txt')

    with open(report_path, 'w', encoding='utf-8', newline='\n') as stream:
        stream.write('\n'.join(report) + '\n')

    print(f'{written} tables written to {out_folder}; {conflict_total} conflicts; report: {report_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

import os
import re
import argparse

def remove_comments_from_code(code_text):
    """
    Удаляет комментарии в стиле C/C++ из строки с кодом.
    """
    # Паттерн для поиска однострочных (//) и многострочных (/* ... */) комментариев.
    # //.*?$          - находит // и все символы до конца строки.
    # \/\*.*?\*\/     - находит /*, затем любые символы (нежадно), и */.
    #
    # re.MULTILINE заставляет `$` работать для каждой строки, а не для всего текста.
    # re.DOTALL заставляет `.` находить также и символы новой строки (важно для /* ... */).
    pattern = r"//.*?$|\/\*.*?\*\/"
    
    regex = re.compile(pattern, re.MULTILINE | re.DOTALL)
    
    def replacer(match):
        # Если найденный комментарий - многострочный, мы можем заменить его
        # на пустую строку. Если однострочный - то тоже на пустую строку,
        # так как символ новой строки останется за пределами совпадения.
        return ""

    return regex.sub(replacer, code_text)

def process_file(file_path):
    """
    Обрабатывает один файл: читает, удаляет комментарии, перезаписывает.
    """
    # Безопасные кодировки для исходников C++. Пробуем utf-8, потом cp1251.
    encodings_to_try = ['utf-8', 'cp1251']
    content = None
    original_encoding = None

    for enc in encodings_to_try:
        try:
            with open(file_path, 'r', encoding=enc) as f:
                content = f.read()
            original_encoding = enc
            break # Успешно прочитали, выходим из цикла
        except UnicodeDecodeError:
            continue # Пробуем следующую кодировку
        except Exception as e:
            print(f" !> Ошибка чтения файла {file_path}: {e}")
            return False

    if content is None:
        print(f" !> Не удалось прочитать файл {file_path} ни в одной из кодировок.")
        return False
        
    cleaned_content = remove_comments_from_code(content)
    
    # Перезаписываем файл, только если в нём были изменения
    if cleaned_content != content:
        try:
            with open(file_path, 'w', encoding=original_encoding) as f:
                f.write(cleaned_content)
            print(f" -> Комментарии удалены из файла: {file_path}")
            return True
        except Exception as e:
            print(f" !> Ошибка записи в файл {file_path}: {e}")
            return False
    else:
        print(f" -> Комментарии не найдены в: {file_path}")
        return False

def main():
    """
    Основная функция для запуска скрипта из командной строки.
    """
    parser = argparse.ArgumentParser(
        description="Рекурсивно удаляет комментарии из файлов C/C++ (.h, .cpp) в указанной директории."
    )
    parser.add_argument("directory", help="Путь к корневой папке проекта.")

    args = parser.parse_args()
    root_dir = args.directory

    if not os.path.isdir(root_dir):
        print(f"Ошибка: Директория '{root_dir}' не найдена.")
        return

    print("=" * 60)
    print("!! ВНИМАНИЕ: Скрипт изменит файлы в указанной директории.")
    print("!! Убедитесь, что у вас есть резервная копия или система контроля версий.")
    print("=" * 60)

    confirm = input(f"Вы уверены, что хотите обработать папку '{root_dir}'? (введите 'yes'): ")
    if confirm.lower() != 'yes':
        print("Операция отменена.")
        return

    modified_files_count = 0
    processed_files_count = 0
    
    # Рекурсивный обход всех папок и файлов
    for dirpath, _, filenames in os.walk(root_dir):
        for filename in filenames:
            # Проверяем расширения файлов
            if filename.endswith(('.cpp', '.h', '.hpp', '.c', '.cxx')):
                file_path = os.path.join(dirpath, filename)
                processed_files_count += 1
                if process_file(file_path):
                    modified_files_count += 1
    
    print("\n--- Готово! ---")
    print(f"Всего обработано файлов: {processed_files_count}")
    print(f"Файлов изменено: {modified_files_count}")

if __name__ == "__main__":
    main()

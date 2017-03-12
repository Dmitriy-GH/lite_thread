﻿# lite_thread.h

Кросплатформенная библиотека на С++11 для легкого распараллеливания кода, в т.ч. потокоНЕбезопасного.

Все исходники и документация в lite_thread.h 

Библиотека построена по Модели Акторов, т.е. общий алгоритм разбивается на акторы (код, атомарные 
части бизнес-логики) и акторы общаются между собой пересылкой сообщений друг-другу. 

Библиотека сама создает нужное количество потоков и запускает в них акторы. 

Библиотека гарантирует что актор не будет запущен одновременно в нескольких потоках, т.е. код актора 
не требует потокобезопасности. Но если актор потокобезопасный, то есть установка ограничения на 
количество одновременно работающих копий данного актора. Также есть ограничение доступа группы акторов 
к конкретному ресурсу (например можно ограничить количество потоков **lite_thread_max(int max)** для 
акторов активно использующих процессор)



### Простейший пример использования

**void actor1(lite_msg_t* msg, void* env) {// Обработчик сообщения }**

void main() {

lite_msg_t* msg = lite_msg_create< int >(); // Создание сообщения 

int* x = lite_msg_data< int >(msg); // Указатель на содержимое сообщения

*x = 100500; // Заполнение сообщения

lite_thread_run(msg, actor1); //Отправка msg в actor1()

 lite_thread_end(); // Ожидание завершения работы

}


### Сообщения
Для создания сообщения нужного размера

**lite_msg_t* msg = lite_msg_create(size, type)**

далее работа с msg->data

Для создания типизированного сообщения (пока только структуры, конструктор не запускается)

**lite_msg_t* msg = lite_msg_create< my_msg_t >()**

Получение указателя на содержимое типизированного сообщения

**my_msg_t* d = lite_msg_data< my_msg_t >(msg)**

далее работа с d->...


###Отправка сообщения

Отправка сообщения актору lite_actor_t* la

**lite_thread_run(msg, la)**

Удаление сообщений происходит автоматически. Библиотека отслеживает было ли входяшее сообщение 
отправлено далее. Если не было, то удаляет.
В случае если необходимо закэшировать сообщение внутри актора, то использовать копирование

**my_cache = lite_msg_copy(msg)**

но после использования необходимо явно удалить

**lite_msg_erase(my_cache)**


### МОЖНО
Менять сообщение и отправлять дальше. Библиотека гарантирует что сообщение обрабатывается 
только одним актором в одном потоке.


### НЕЛЬЗЯ
- Читать/писать сообщение после отправки, т.к. оно может быть уже в обработке получателем или удалено.
- Отправлять одно сообщение дважды.


### Рекомендации использования
Для избежания лишнего выделения памяти в начале лучше создать сообщение, учитывающее всю цепочку 
акторов, через которое оно пройдет, а в процессе прохождения цепочки каждый актор заполняет свою
часть сообщения и отправляет дальше. В реальности память будет выделена в момент отправки, а акторы 
будут просто пересылать указатель.


### Акторы
это или функция

**void recv(lite_msg_t* msg, void* env) {
 ... Обработка сообщения
}**

или класс, унаследованный от базового класса lite_worker_t 

**class my_worker_t : public lite_worker_t {
   void recv(lite_msg_t* msg) override {
       ... Обработка сообщения
   }
}**

Создание объектов класса делает библиотека

**lite_actor_t* lite_actor_create< my_worker_t >("my name")**

поэтому не надо отслеживать указатели, акторы-объекты будут удалены автоматичести по завершению работы.

Именование акторов позволяет в любом месте кода восстановить хэндл актора по имени

**lite_actor_t* lite_actor_get("my name")**

При использовании класса можно дополнительно установить фильтр только на известные типы сообщений:

**type_add(lite_msg_type< T >())**

При установленном фильтре, при получении сообщения необрабатываемого типа произойдет отправка 
сообщения с ошибкой на обработчик ошибок.

### Ограничение ресурсов

Ресурс ограничивает сколько может быть запущено одновременно акторов привязанных к ресурсу. 
При старте создается ресурс по умолчанию и вновь создаваемые акторы привязываются к нему.
Рекомендуется использовать ресурс по умолчанию для акторов нагружающих процессор. 
Соответственно максимум этого ресурса - количество потоков, которое можно использовать.
Для акторов не нагружающих процессор (например запрос к вэб-сервису и ожидание ответа) 
лучше создать отдельный ресурс и привязать туда эти акторы.

Установка ресурса по умолчанию:

**lite_thread_max(int max)**

**Ограничение:** актор может быть привязан только к одному ресурсу.


### Обработка ошибок

Отправка сообщения об ошибке
**lite_error(const char* data, ...)**

Есть специальный актор с именем "error" на который приходят все сообщения от ошибках, в т.ч. библиотеки.
Можно создать собственный актор "error" и тогда ошибки пойдут туда.


### Примеры использования

**test** примеры простейшего использования с выводом в консоль результата работы

**stress_test** нагрузочный тест для проверки работоспособности библиотеки

**card_raytracer** пример преобразования обычного последовательного алгоритма в алгоритм с использованием 
акторов и многопоточности.



Подробная документация в начале lite_thread.h

(define defmacro
  (macro (name parameters &rest body)
    `(define ,name (macro ,parameters ,@body))))

(defmacro defun (name parameters &rest body)
  `(define ,name (lambda ,parameters ,@body)))

(defmacro defsmacro (name parameters &rest body)
  `(defmacro ,name ,parameters ,@body))

(defmacro desfun (name parameters &rest body)
  `(defun ,name ,parameters ,@body))

(defmacro if (condition consequent alternate)
  `(cond (,condition ,consequent) (#t ,alternate)))

(defmacro when (condition &rest body)
  `(if ,condition (begin ,@body) #f))

(defmacro unless (condition &rest body)
  `(if ,condition #f (begin ,@body)))

(defun not (value)
  (if value #f #t))

(defun list (&rest values) values)

(defun length (values)
  (if (null? values)
      0
      (+ 1 (length (cdr values)))))

(defun reverse (values)
  (let ((input values) (result '()))
    (while (not (null? input))
      (set! result (cons (car input) result))
      (set! input (cdr input)))
    result))

(defun append (left right)
  (if (null? left)
      right
      (cons (car left) (append (cdr left) right))))

(defun map (function values)
  (if (null? values)
      '()
      (cons (function (car values))
            (map function (cdr values)))))

(defun for-each (function values)
  (if (null? values)
      '()
      (begin
        (function (car values))
        (for-each function (cdr values)))))

(defun filter (predicate values)
  (cond ((null? values) '())
        ((predicate (car values))
         (cons (car values) (filter predicate (cdr values))))
        (#t (filter predicate (cdr values)))))

(defun fold-left (function initial values)
  (if (null? values)
      initial
      (fold-left function
                 (function initial (car values))
                 (cdr values))))

(defun member (value values)
  (cond ((null? values) #f)
        ((eq? value (car values)) values)
        (#t (member value (cdr values)))))

(defun assoc (key values)
  (cond ((null? values) #f)
        ((eq? key (car (car values))) (car values))
        (#t (assoc key (cdr values)))))

(defmacro letrec (bindings &rest body)
  `(let ,(map (lambda (binding) `(,(car binding) #f)) bindings)
     ,@(map (lambda (binding)
              `(set! ,(car binding) ,(cadr binding)))
            bindings)
     ,@body))

(defun caar (value) (car (car value)))
(defun cadr (value) (car (cdr value)))
(defun cdar (value) (cdr (car value)))
(defun cddr (value) (cdr (cdr value)))
(defun caddr (value) (car (cdr (cdr value))))
(defun cadddr (value) (car (cdr (cdr (cdr value)))))

(defmacro push! (place value)
  `(set! ,place (cons ,value ,place)))

(defmacro pop! (place)
  `(set! ,place (cdr ,place)))

(defmacro inc! (place)
  `(set! ,place (+ ,place 1)))

(defmacro dec! (place)
  `(set! ,place (- ,place 1)))

(defmacro pair? (value) `(type? 'pair ,value))
(defmacro symbol? (value) `(type? 'symbol ,value))
(defmacro string? (value) `(type? 'string ,value))
(defmacro integer? (value) `(type? 'integer ,value))
(defmacro float? (value) `(type? 'float ,value))
(defmacro array? (value) `(type? 'array ,value))
(defmacro number? (value) `(or (integer? ,value) (float? ,value)))
(defmacro zero? (value) `(= ,value 0))

(define __goos-lib-loaded__ #t)

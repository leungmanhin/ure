scm
; 
; disjunct-list.scm
;
; Build lists of link-grammar disjuncts; update the SQL
; database counts with the results.
;
; This is also a part of a test exploring different coding styles.
; Similar fuunctionality is (paritally) implemented in disjunct.scm,
; but using a different approach.
;
; Copyright (c) 2008 Linas Vepstas <linasvepstas@gmail.com>
;
; =============================================================
; List-style disjuncts
;
; Return a list of all of the link-grammar links the word particpates in
(define (get-lg-rels word)

	(define (get-rel word-pair)
		;; Take the car, because cog-get-link returns a list of links.
		;; We expect this list to only contain one element, in total.
		(car (cog-get-link 'EvaluationLink 'LinkGrammarRelationshipNode word-pair))
	)

	(map get-rel (cog-filter-incoming 'ListLink word))
)

; ---------------------------------------------------------------------
; Get list of all words in the sentence
(define (get-word-list sent)
	(reverse! (cog-chase-link 'SentenceLink 'ConceptNode sent))
)

; ---------------------------------------------------------------------
; Given a word, and the sentence in which the word appears, return
; a list of the ling-grammar relations in which the word appears. 
; The relations are sorted in sentence word-order.
;
; Similar to get-lg-rels, but sorted.
;
(define (get-lg-rels-sorted word sent-node)

	; rel-list is a list of the link-grammar relations.
	(let ((rel-list (get-lg-rels word)))

		; Compare two link-grammar relations, and determine thier sentence
		; word order.
		(define (wless? rel-a rel-b)

			; Return the index of the word in a sentence
			(define (windex wrd)
				(if (equal? "LEFT-WALL" (cog-name wrd))
					-1
					(list-index (lambda (w) (equal? wrd w)) (get-word-list sent-node))
				)
			)

			(let ((word-a (cog-pred-get-partner rel-a word))
					(word-b (cog-pred-get-partner rel-b word))
				)
				(< (windex word-a) (windex word-b))
			)
		)
		(sort rel-list wless?)
	)
)

; ---------------------------------------------------------------------
; Return a string listing all of the link-grammar relations for the
; word in the sentence. This string will be in proper sorted order,
; according to the appearence of the words in the sentence word-order.
(define (make-disjunct sorted-rels)

	; Given a single opencog predicate (EvaluationLink) triple
	; containing a link-grammar relation, just return the relation name,
	; as a string.
	(define (get-name rel)
		(cog-name (car (cog-filter-outgoing 'LinkGrammarRelationshipNode rel)))
	)

	; Given a list of names, create a string, padding it with blanks.
	(define (mk-dj-string name-list str)
		(if (null? name-list)
			str
			(mk-dj-string (cdr name-list) (string-append str (car name-list) " "))
		)
	)

	(mk-dj-string (map get-name sorted-rels) "")
)

; ---------------------------------------------------------------------
;
; Process the disjuncts for a single word in a sentence
;
(define (process-disjunct word sent-node)

	; Return the word string associated with the wor-instance
	(define (get-word word-inst)
		(cog-name (car (cog-chase-link 'ReferenceLink 'WordNode word-inst)))
	)
(display "Word: ")
(display (get-word word))
(display " -- ")
(display (make-disjunct (get-lg-rels-sorted word sent-node)))
(display "\n")
)

; ---------------------------------------------------------------------
; Given a single sentence, process the disjuncts for that sentence
(define (list-get-sentence-disjuncts sent-node)

	; process the disjunct for this word.
	(define (mk-disjunct word)
		(process-disjunct word sent-node)
	)

	(for-each mk-disjunct (get-word-list sent-node))
)

; ---------------------------------------------------------------------
; Given a list of sentences, process each sentence to extract disjuncts
(define (list-get-sentence-list-disjuncts sent-list)

	; Loop over a list of sentences, getting disjuncts for each sentence.
	(for-each list-get-sentence-disjuncts sent-list)
)


; ---------------------------------------------------------------------
; Do it list-style
(define (list-it) 
	(list-get-sentence-list-disjuncts (cog-get-atoms 'SentenceNode))
)

; =====================================================================


;; =====================================================================
;; Implication introduction rule
;; 
;; P
;; Q
;; |-
;; ImplicationLink
;;    P
;;    Q
;;
;; To properly deal with this we should support deep type and infer
;; that P and Q have non empty intersection.
;;
;; Overlaps:
;;
;; - can probably be replaced by a deduction rule with B == Universe
;;
;; ----------------------------------------------------------------------

(define implication-introduction-variables
  (VariableSet
     (TypedVariableLink
        (VariableNode "$P")
        (TypeChoice
           (TypeNode "PredicateNode")
           (TypeNode "LambdaLink")))
     (TypedVariableLink
        (VariableNode "$Q")
        (TypeChoice
           (TypeNode "PredicateNode")
           (TypeNode "LambdaLink")))))

(define implication-introduction-body
  (AndLink
     (PresentLink
        (VariableNode "$P")
        (VariableNode "$Q"))
     (NotLink
        (IdenticalLink
           (VariableNode "$P")
           (VariableNode "$Q")))))

(define implication-introduction-rewrite
  (ExecutionOutputLink
     (GroundedSchemaNode "scm: implication-introduction")
     (ListLink
        (VariableNode "$P")
        (VariableNode "$Q"))))

(define implication-introduction-rule
  (BindLink
     implication-introduction-variables
     implication-introduction-body
     implication-introduction-rewrite))

(define (implication-introduction P Q)
  (let* (
         (P-s (cog-mean P))
         (P-c (cog-confidence P))
         (Q-s (cog-mean Q))
         (Q-c (cog-confidence Q))
         ; Compute the implication TV
         (Impl-s Q-s)
         (Impl-c (if (< 0.9 (* Q-s Q-c)) ; Hack to overcome the lack
                                         ; of distributional TV
                        Q-c
                        (* P-c Q-c)))) ; Big hack because the naive
                                       ; formula sucks
    (if (< 0 Impl-c) ; Try to avoid constructing informationless
                     ; knowledge
        (cog-merge-hi-conf-tv!
         (ImplicationLink
            P
            Q)
         (cog-new-stv Impl-s Impl-c)))))

;; Name the rule
(define implication-introduction-rule-name
  (DefinedSchemaNode "implication-introduction-rule"))
(DefineLink implication-introduction-rule-name
  implication-introduction-rule)

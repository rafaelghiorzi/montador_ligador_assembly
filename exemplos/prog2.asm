; Modulo somador: imprime N1 + N2 e volta para FIM, no modulo principal.
SOMADOR: BEGIN
SECAO TEXT
N1:    EXTERN
N2:    EXTERN
FIM:   EXTERN
PUBLIC SOMADOR
       LOAD N1
       ADD N2
       STORE RESULTADO
       OUTPUT RESULTADO
       JMP FIM
SECAO DATA
RESULTADO: SPACE
END

todo
------

(vide — les deux chantiers ci-dessous sont faits, non commités)


done
------

1- [x] alléger le display du blotter :
    - [x] grecques foldable (cachées par défaut, toggle "Greeks" dans la toolbar)
    - [x] ordre des colonnes changeable (drag des en-têtes, poignée par colonne)
    - [x] 2 chiffres après la virgule

2- [x] états du workflow visibles dans le blotter (colonne Status + colonne action) :
    - [x] new : créée dans le panel — toute option pricée dans un panel est
          systématiquement mirrorée dans le blotter (une seule ligne qui évolue)
    - [x] quoting : en cours de pricing par l'engine
    - [x] quoted : pricée, en attente de la décision sales
    - [x] traded : à la main du sales (colonne action), gelée
    - [x] missed : à la main du sales (colonne action), gelée
    - [x] undo pour rouvrir une ligne mal marquée
    => seul le rôle sales est modélisé ; trader/client plus tard
